package org.mozilla.fenix.downloads.dialog

import android.app.Activity
import android.content.Context
import android.view.Gravity
import android.view.View
import android.widget.FrameLayout
import androidx.coordinatorlayout.widget.CoordinatorLayout
import androidx.core.view.children
import androidx.core.view.isVisible
import io.mockk.every
import io.mockk.mockk
import io.mockk.mockkStatic
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.utils.Settings
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class StartDownloadDialogTest {
    @Test
    fun `WHEN the view is to be shown THEN set the scrim and other window customization bind the download values`() {
        val activity = Robolectric.buildActivity(Activity::class.java).create().get()
        val dialogParent = FrameLayout(testContext)
        val dialogContainer = FrameLayout(testContext).also {
            dialogParent.addView(it)
            it.layoutParams = CoordinatorLayout.LayoutParams(0, 0)
        }
        val dialog = TestDownloadDialog(activity)

        mockkStatic(
            "mozilla.components.support.ktx.android.view.WindowKt",
            "org.mozilla.fenix.ext.ContextKt",
        ) {
            every { any<Context>().settings() } returns mockk(relaxed = true)
            every { any<Context>().components } returns mockk(relaxed = true)
            val fluentDialog = dialog.show(dialogContainer)

            val scrim = dialogParent.children.first { it.id == R.id.scrim }
            assertTrue(scrim.hasOnClickListeners())
            assertFalse(scrim.isSoundEffectsEnabled)
            assertTrue(dialog.wasDownloadDataBinded)
            assertEquals(
                Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL,
                (dialogContainer.layoutParams as CoordinatorLayout.LayoutParams).gravity,
            )
            assertEquals(
                testContext.resources.getDimension(R.dimen.browser_fragment_above_toolbar_panels_elevation),
                dialogContainer.elevation,
            )
            assertTrue(dialogContainer.isVisible)
            assertEquals(dialog, fluentDialog)
        }
    }

    @Test
    fun `GIVEN a dismiss callback WHEN the dialog is dismissed THEN the callback is informed`() {
        var wasDismissCalled = false
        val activity = Robolectric.buildActivity(Activity::class.java).create().get()
        val dialog = TestDownloadDialog(activity)
        mockkStatic("org.mozilla.fenix.ext.ContextKt") {
            every { any<Context>().components } returns mockk(relaxed = true)
            val fluentDialog = dialog.onDismiss { wasDismissCalled = true }
            dialog.onDismiss()

            assertTrue(wasDismissCalled)
            assertEquals(dialog, fluentDialog)
        }
    }

    @Test
    fun `GIVEN the download dialog is shown WHEN dismissed THEN remove the scrim, the dialog and any window customizations`() {
        val activity = Robolectric.buildActivity(Activity::class.java).create().get()
        val dialogParent = FrameLayout(testContext)
        val dialogContainer = FrameLayout(testContext).also {
            dialogParent.addView(it)
            it.layoutParams = CoordinatorLayout.LayoutParams(0, 0)
        }
        val dialog = TestDownloadDialog(activity)
        mockkStatic(
            "mozilla.components.support.ktx.android.view.WindowKt",
            "org.mozilla.fenix.ext.ContextKt",
        ) {
            every { any<Context>().settings() } returns mockk(relaxed = true)
            every { any<Context>().components } returns mockk(relaxed = true)
            dialog.show(dialogContainer)

            dialog.dismiss()

            Assert.assertNull(dialogParent.children.firstOrNull { it.id == R.id.scrim })
            assertTrue(dialogParent.childCount == 1)
            assertTrue(dialogContainer.childCount == 0)
            assertFalse(dialogContainer.isVisible)
        }
    }

    @Test
    fun `GIVEN a ViewGroup WHEN enabling accessibility THEN enable it for all children but the dialog container`() {
        val activity: Activity = mockk(relaxed = true)
        val dialogParent = FrameLayout(testContext)
        FrameLayout(testContext).also {
            dialogParent.addView(it)
            it.id = R.id.startDownloadDialogContainer
            it.importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
        }
        val otherView = View(testContext).also {
            dialogParent.addView(it)
            it.importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
        }
        val dialog = TestDownloadDialog(activity)

        dialog.enableSiblingsAccessibility(dialogParent)

        assertEquals(
            listOf(otherView),
            dialogParent.children.filter { it.isImportantForAccessibility }.toList(),
        )
    }

    @Test
    fun `GIVEN a ViewGroup WHEN disabling accessibility THEN disable it for all children but the dialog container`() {
        val activity: Activity = mockk(relaxed = true)
        val dialogParent = FrameLayout(testContext)
        val dialogContainer = FrameLayout(testContext).also {
            dialogParent.addView(it)
            it.id = R.id.startDownloadDialogContainer
            it.importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_YES
        }
        View(testContext).also {
            dialogParent.addView(it)
            it.importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_YES
        }
        val dialog = TestDownloadDialog(activity)

        dialog.disableSiblingsAccessibility(dialogParent)

        assertEquals(
            listOf(dialogContainer),
            dialogParent.children.filter { it.isImportantForAccessibility }.toList(),
        )
    }

    @Test
    fun `GIVEN accessibility services are enabled WHEN the dialog is shown THEN disable siblings accessibility`() {
        val activity = Robolectric.buildActivity(Activity::class.java).create().get()
        val dialogParent = FrameLayout(testContext)
        val dialogContainer = FrameLayout(testContext).also {
            dialogParent.addView(it)
            it.id = R.id.startDownloadDialogContainer
            it.layoutParams = CoordinatorLayout.LayoutParams(0, 0)
            it.importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_YES
        }
        View(testContext).also {
            dialogParent.addView(it)
            it.importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_YES
        }

        mockkStatic("org.mozilla.fenix.ext.ContextKt") {
            val dialog = TestDownloadDialog(activity)

            val settings: Settings = mockk {
                every { accessibilityServicesEnabled } returns false
            }
            every { any<Context>().settings() } returns settings
            every { any<Context>().components } returns mockk(relaxed = true)
            dialog.show(dialogContainer)
            assertEquals(2, dialogParent.children.count { it.isImportantForAccessibility })

            every { settings.accessibilityServicesEnabled } returns true
            dialog.show(dialogContainer)
            assertEquals(
                listOf(dialogContainer),
                dialogParent.children.filter { it.isImportantForAccessibility }.toList(),
            )
        }
    }

    @Test
    fun `WHEN the dialog is dismissed THEN re-enable siblings accessibility`() {
        val activity = Robolectric.buildActivity(Activity::class.java).create().get()
        val dialogParent = FrameLayout(testContext)
        val dialogContainer = FrameLayout(testContext).also {
            dialogParent.addView(it)
            it.id = R.id.startDownloadDialogContainer
            it.layoutParams = CoordinatorLayout.LayoutParams(0, 0)
            it.importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_YES
        }
        val accessibleView = View(testContext).also {
            dialogParent.addView(it)
            it.importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_YES
        }
        mockkStatic("org.mozilla.fenix.ext.ContextKt") {
            val settings: Settings = mockk {
                every { accessibilityServicesEnabled } returns true
            }
            every { any<Context>().settings() } returns settings
            every { any<Context>().components } returns mockk(relaxed = true)
            val dialog = TestDownloadDialog(activity)
            dialog.show(dialogContainer)

            dialog.dismiss()

            assertEquals(
                listOf(accessibleView),
                dialogParent.children.filter { it.isVisible && it.isImportantForAccessibility }
                    .toList(),
            )
        }
    }
}

private class TestDownloadDialog(
    activity: Activity,
) : StartDownloadDialog(activity) {
    var wasDownloadDataBinded = false

    override fun setupView() {
        wasDownloadDataBinded = true
    }
}
