/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.media.service

import android.app.ForegroundServiceStartNotAllowedException
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.media.AudioManager
import android.os.Build
import android.support.v4.media.MediaMetadataCompat
import android.support.v4.media.session.MediaSessionCompat
import androidx.annotation.VisibleForTesting
import androidx.core.content.ContextCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.concept.engine.mediasession.MediaSession
import mozilla.components.feature.media.ext.getArtistOrUrl
import mozilla.components.feature.media.ext.getNonPrivateIcon
import mozilla.components.feature.media.ext.getTitleOrUrl
import mozilla.components.feature.media.ext.toPlaybackState
import mozilla.components.feature.media.facts.emitNotificationPauseFact
import mozilla.components.feature.media.facts.emitNotificationPlayFact
import mozilla.components.feature.media.facts.emitStatePauseFact
import mozilla.components.feature.media.facts.emitStatePlayFact
import mozilla.components.feature.media.facts.emitStateStopFact
import mozilla.components.feature.media.focus.AudioFocus
import mozilla.components.feature.media.notification.MediaNotification
import mozilla.components.feature.media.session.MediaSessionCallback
import mozilla.components.support.base.android.NotificationsDelegate
import mozilla.components.support.base.ids.SharedIdsHelper
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.utils.ext.registerReceiverCompat
import mozilla.components.support.utils.ext.stopForegroundCompat
import kotlin.coroutines.CoroutineContext
import kotlin.coroutines.EmptyCoroutineContext

@VisibleForTesting
internal class BecomingNoisyReceiver(private val controller: MediaSession.Controller?) : BroadcastReceiver() {
    override fun onReceive(context: Context?, intent: Intent) {
        if (AudioManager.ACTION_AUDIO_BECOMING_NOISY == intent.action) {
            controller?.pause()
        }
    }

    @VisibleForTesting
    fun deviceIsBecomingNoisy(context: Context) {
        val becomingNoisyIntent = Intent(AudioManager.ACTION_AUDIO_BECOMING_NOISY)
        onReceive(context, becomingNoisyIntent)
    }
}

/**
 * Delegate handling callbacks from an [AbstractMediaSessionService].
 *
 * The implementation was moved from [AbstractMediaSessionService] to this delegate for better testability.
 */
internal class MediaSessionServiceDelegate(
    @get:VisibleForTesting internal var context: Context,
    @get:VisibleForTesting internal val service: AbstractMediaSessionService,
    @get:VisibleForTesting internal val store: BrowserStore,
    @get:VisibleForTesting internal val crashReporter: CrashReporting?,
    @get:VisibleForTesting internal val notificationsDelegate: NotificationsDelegate,
) : MediaSessionDelegate {
    private val logger = Logger("MediaSessionService")

    @VisibleForTesting
    internal var notificationHelper = MediaNotification(context, service::class.java)

    @VisibleForTesting
    internal var mediaSession = MediaSessionCompat(context, "MozacMediaSession")

    @VisibleForTesting
    internal var audioFocus = AudioFocus(context.getSystemService(Context.AUDIO_SERVICE) as AudioManager, store)

    @VisibleForTesting
    internal val notificationId by lazy {
        SharedIdsHelper.getIdForTag(context, AbstractMediaSessionService.NOTIFICATION_TAG)
    }

    @VisibleForTesting
    internal var controller: MediaSession.Controller? = null

    @VisibleForTesting
    internal var notificationScope: CoroutineScope? = null

    @VisibleForTesting
    internal val intentFilter = IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY)

    @VisibleForTesting
    internal var noisyAudioStreamReceiver: BecomingNoisyReceiver? = null

    @VisibleForTesting
    internal var isForegroundService: Boolean = false

    fun onCreate() {
        logger.debug("Service created")
        mediaSession.setCallback(MediaSessionCallback(store))
        notificationScope = MainScope()
    }

    fun onDestroy() {
        notificationScope?.cancel()
        notificationScope = null
        audioFocus.abandon()
        logger.debug("Service destroyed")
    }

    fun onStartCommand(intent: Intent?) {
        logger.debug("Command received: ${intent?.action}")

        when (intent?.action) {
            AbstractMediaSessionService.ACTION_PLAY -> {
                controller?.play()
                emitNotificationPlayFact()
            }
            AbstractMediaSessionService.ACTION_PAUSE -> {
                controller?.pause()
                emitNotificationPauseFact()
            }
            else -> logger.debug("Can't process action: ${intent?.action}")
        }
    }

    fun onTaskRemoved() {
        // no need to do this for custom tabs
        store.state.tabs.forEach {
            it.mediaSessionState?.controller?.stop()
        }

        shutdown()
    }

    override fun handleMediaPlaying(sessionState: SessionState) {
        emitStatePlayFact()

        updateMediaSession(sessionState)
        registerBecomingNoisyListenerIfNeeded(sessionState)
        audioFocus.request(sessionState.id)
        controller = sessionState.mediaSessionState?.controller

        if (isForegroundService) {
            updateNotification(sessionState)
        } else {
            startForeground(sessionState)
        }
    }

    override fun handleMediaPaused(sessionState: SessionState) {
        emitStatePauseFact()

        updateMediaSession(sessionState)
        unregisterBecomingNoisyListenerIfNeeded()
        stopForeground()

        updateNotification(sessionState)
    }

    override fun handleMediaStopped(sessionState: SessionState) {
        emitStateStopFact()

        updateMediaSession(sessionState)
        unregisterBecomingNoisyListenerIfNeeded()
        stopForeground()

        updateNotification(sessionState)
    }

    override fun handleNoMedia() {
        shutdown()
    }

    @VisibleForTesting
    internal fun updateNotification(sessionState: SessionState) {
        notificationScope?.launch {
            val notification = notificationHelper.create(sessionState, mediaSession)
            notificationsDelegate.notify(
                notificationId = notificationId,
                notification = notification,
            )
        }
    }

    @VisibleForTesting
    @Suppress("TooGenericExceptionCaught")
    internal fun startForeground(
        sessionState: SessionState,
        coroutineContext: CoroutineContext = EmptyCoroutineContext,
    ) {
        notificationScope?.launch(coroutineContext) {
            val notification = notificationHelper.create(sessionState, mediaSession)
            try {
                service.startForeground(notificationId, notification)
            } catch (e: Exception) {
                if (
                    Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
                    e is ForegroundServiceStartNotAllowedException
                ) {
                    // We should not encounter this exception if `android:foregroundServiceType="mediaPlayback"`
                    // is added to the service. The crash reporter loses the stack trace for this
                    // exception so we want to be able to track this crash independently to ensure
                    // this case is fixed and be able to determine if there are other cases where we
                    // might be trying to start foreground services from the background.
                    // https://bugzilla.mozilla.org/show_bug.cgi?id=1802620
                    crashReporter?.submitCaughtException(e)
                } else {
                    throw e
                }
            }

            isForegroundService = true
        }
    }

    @VisibleForTesting
    internal fun updateMediaSession(sessionState: SessionState) {
        mediaSession.setPlaybackState(sessionState.mediaSessionState?.toPlaybackState())
        mediaSession.isActive = true
        notificationScope?.launch {
            mediaSession.setMetadata(
                MediaMetadataCompat.Builder()
                    .putString(
                        MediaMetadataCompat.METADATA_KEY_TITLE,
                        sessionState.getTitleOrUrl(context, sessionState.mediaSessionState?.metadata?.title),
                    )
                    .putString(
                        MediaMetadataCompat.METADATA_KEY_ARTIST,
                        sessionState.getArtistOrUrl(sessionState.mediaSessionState?.metadata?.artist),
                    )
                    .putBitmap(
                        MediaMetadataCompat.METADATA_KEY_ART,
                        sessionState.getNonPrivateIcon(sessionState.mediaSessionState?.metadata?.getArtwork),
                    )
                    .putLong(MediaMetadataCompat.METADATA_KEY_DURATION, -1)
                    .build(),
            )
        }
    }

    @VisibleForTesting
    internal fun stopForeground() {
        service.stopForegroundCompat(false)
        isForegroundService = false
    }

    @VisibleForTesting
    internal fun registerBecomingNoisyListenerIfNeeded(state: SessionState) {
        if (noisyAudioStreamReceiver != null) {
            return
        }

        noisyAudioStreamReceiver = BecomingNoisyReceiver(state.mediaSessionState?.controller)
        noisyAudioStreamReceiver?.let {
            registerBecomingNoisyListener(it)
        }
    }

    @VisibleForTesting
    internal fun registerBecomingNoisyListener(broadcastReceiver: BroadcastReceiver) {
        context.registerReceiverCompat(
            broadcastReceiver,
            intentFilter,
            ContextCompat.RECEIVER_NOT_EXPORTED,
        )
    }

    @VisibleForTesting
    internal fun unregisterBecomingNoisyListenerIfNeeded() {
        noisyAudioStreamReceiver?.let {
            context.unregisterReceiver(noisyAudioStreamReceiver)
            noisyAudioStreamReceiver = null
        }
    }

    @VisibleForTesting
    internal fun shutdown() {
        mediaSession.release()
        // Explicitly cancel media notification.
        // Otherwise, when media is paused, with [STOP_FOREGROUND_DETACH] notification behavior,
        // the notification will persist even after service is stopped and destroyed.
        notificationsDelegate.notificationManagerCompat.cancel(notificationId)
        unregisterBecomingNoisyListenerIfNeeded()
        service.stopSelf()
    }

    @VisibleForTesting
    internal fun deviceBecomingNoisy(context: Context) {
        noisyAudioStreamReceiver?.deviceIsBecomingNoisy(context)
    }
}
