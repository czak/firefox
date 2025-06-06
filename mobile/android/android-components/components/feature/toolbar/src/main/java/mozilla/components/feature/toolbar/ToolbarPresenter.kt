/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.toolbar

import androidx.annotation.VisibleForTesting
import androidx.annotation.VisibleForTesting.Companion.PRIVATE
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.distinctUntilChangedBy
import mozilla.components.browser.state.selector.findCustomTabOrSelectedTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.toolbar.Toolbar
import mozilla.components.concept.toolbar.Toolbar.Highlight
import mozilla.components.concept.toolbar.Toolbar.SiteTrackingProtection
import mozilla.components.feature.toolbar.internal.URLRenderer
import mozilla.components.lib.state.ext.flowScoped
import mozilla.components.support.ktx.kotlin.isContentUrl

/**
 * Presenter implementation for a toolbar implementation in order to update the toolbar whenever
 * the state of the selected session.
 */
class ToolbarPresenter(
    private val toolbar: Toolbar,
    private val store: BrowserStore,
    private val customTabId: String? = null,
    private val shouldDisplaySearchTerms: Boolean = false,
    urlRenderConfiguration: ToolbarFeature.UrlRenderConfiguration? = null,
) {
    @VisibleForTesting
    internal var renderer = URLRenderer(toolbar, urlRenderConfiguration)

    private var scope: CoroutineScope? = null

    /**
     * Start presenter: Display data in toolbar.
     */
    fun start() {
        renderer.start()

        scope = store.flowScoped { flow ->
            flow.distinctUntilChangedBy { it.findCustomTabOrSelectedTab(customTabId) }
                .collect { state ->
                    render(state)
                }
        }
    }

    fun stop() {
        scope?.cancel()
        renderer.stop()
    }

    @VisibleForTesting(otherwise = PRIVATE)
    internal fun render(state: BrowserState) {
        val tab = state.findCustomTabOrSelectedTab(customTabId)

        if (tab != null) {
            if (shouldDisplaySearchTerms && tab.content.searchTerms.isNotBlank()) {
                toolbar.url = tab.content.searchTerms
            } else {
                renderer.post(tab.content.url)
            }

            toolbar.setSearchTerms(tab.content.searchTerms)
            toolbar.displayProgress(tab.content.progress)

            toolbar.siteInfo = if (tab.content.url.isContentUrl()) {
                Toolbar.SiteInfo.LOCAL_PDF
            } else if (tab.content.securityInfo.secure) {
                Toolbar.SiteInfo.SECURE
            } else {
                Toolbar.SiteInfo.INSECURE
            }

            toolbar.siteTrackingProtection = when {
                tab.trackingProtection.ignoredOnTrackingProtection -> SiteTrackingProtection.OFF_FOR_A_SITE
                tab.trackingProtection.enabled && tab.trackingProtection.blockedTrackers.isNotEmpty() ->
                    SiteTrackingProtection.ON_TRACKERS_BLOCKED

                tab.trackingProtection.enabled -> SiteTrackingProtection.ON_NO_TRACKERS_BLOCKED

                else -> SiteTrackingProtection.OFF_GLOBALLY
            }

            updateHighlight(tab)
        } else {
            clear()
        }
    }

    private fun updateHighlight(tab: SessionState) {
        toolbar.highlight = when {
            tab.content.permissionHighlights.permissionsChanged ||
                tab.trackingProtection.ignoredOnTrackingProtection
            -> Highlight.PERMISSIONS_CHANGED
            else -> Highlight.NONE
        }
    }

    @VisibleForTesting(otherwise = PRIVATE)
    internal fun clear() {
        renderer.post("")

        toolbar.setSearchTerms("")
        toolbar.displayProgress(0)

        toolbar.siteInfo = Toolbar.SiteInfo.INSECURE

        toolbar.siteTrackingProtection = SiteTrackingProtection.OFF_GLOBALLY
        toolbar.highlight = Highlight.NONE
    }
}
