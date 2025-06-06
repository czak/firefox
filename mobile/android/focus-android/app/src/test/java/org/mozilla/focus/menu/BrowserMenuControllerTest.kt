/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.menu

import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.feature.top.sites.TopSitesUseCases
import mozilla.components.support.test.any
import mozilla.components.support.test.mock
import org.junit.Before
import org.junit.Test
import org.mockito.Mock
import org.mockito.Mockito
import org.mockito.Mockito.doNothing
import org.mockito.Mockito.spy
import org.mockito.Mockito.times
import org.mockito.MockitoAnnotations
import org.mozilla.focus.browser.integration.BrowserMenuController
import org.mozilla.focus.state.AppStore

class BrowserMenuControllerTest {
    private lateinit var browserMenuController: BrowserMenuController

    @Mock
    private lateinit var sessionUseCases: SessionUseCases

    @Mock
    private lateinit var appStore: AppStore

    @Mock
    private lateinit var topSitesUseCases: TopSitesUseCases

    private val currentTabId: String = "1"
    private val selectedTab = createTab("https://www.mozilla.org", id = "1")
    private val shareCallback: () -> Unit = {}

    @Mock
    private lateinit var requestDesktopCallback: (isChecked: Boolean) -> Unit

    @Mock
    private lateinit var addToHomeScreenCallback: () -> Unit

    @Mock
    private lateinit var showFindInPageCallback: () -> Unit

    @Mock
    private lateinit var openInCallback: () -> Unit

    // NB: we should avoid mocking lambdas..
    @Mock
    private lateinit var openInBrowser: () -> Unit

    @Mock
    private lateinit var showShortcutAddedSnackBar: () -> Unit

    @Before
    fun setup() {
        val store = BrowserStore(
            initialState = BrowserState(
                tabs = listOf(selectedTab),
                selectedTabId = selectedTab.id,
            ),
        )

        MockitoAnnotations.openMocks(this)

        browserMenuController = spy(
            BrowserMenuController(
                sessionUseCases = sessionUseCases,
                appStore = appStore,
                store = store,
                topSitesUseCases = topSitesUseCases,
                currentTabId = currentTabId,
                shareCallback = shareCallback,
                requestDesktopCallback = requestDesktopCallback,
                addToHomeScreenCallback = addToHomeScreenCallback,
                showFindInPageCallback = showFindInPageCallback,
                openInCallback = openInCallback,
                openInBrowser = openInBrowser,
                showShortcutAddedSnackBar = showShortcutAddedSnackBar,
                coroutineScope = TestScope(StandardTestDispatcher()),
            ),
        )

        doNothing().`when`(browserMenuController).recordBrowserMenuTelemetry(any())
        doNothing().`when`(browserMenuController).addToShortcuts()

        val stopLoadingUseCase: SessionUseCases.StopLoadingUseCase = mock()
        Mockito.`when`(sessionUseCases.stopLoading).thenReturn(stopLoadingUseCase)

        val goBackUseCase: SessionUseCases.GoBackUseCase = mock()
        Mockito.`when`(sessionUseCases.goBack).thenReturn(goBackUseCase)

        val goForwardUseCase: SessionUseCases.GoForwardUseCase = mock()
        Mockito.`when`(sessionUseCases.goForward).thenReturn(goForwardUseCase)

        val reloadUseCase: SessionUseCases.ReloadUrlUseCase = mock()
        Mockito.`when`(sessionUseCases.reload).thenReturn(reloadUseCase)
    }

    @Test
    fun `GIVEN Back menu item WHEN the item is pressed THEN goBack use case is called`() {
        val menuItem = ToolbarMenu.Item.Back
        browserMenuController.handleMenuInteraction(menuItem)
        Mockito.verify(sessionUseCases, times(1)).goBack
    }

    @Test
    fun `GIVEN Forward menu item WHEN the item is pressed THEN goForward use case is called`() {
        val menuItem = ToolbarMenu.Item.Forward
        browserMenuController.handleMenuInteraction(menuItem)
        Mockito.verify(sessionUseCases, times(1)).goForward
    }

    @Test
    fun `GIVEN Reload menu item WHEN the item is pressed THEN reload use case is called`() {
        val menuItem = ToolbarMenu.Item.Reload
        browserMenuController.handleMenuInteraction(menuItem)
        Mockito.verify(sessionUseCases, times(1)).reload
    }

    @Test
    fun `GIVEN Stop menu item WHEN the item is pressed THEN stopLoading use case is called`() {
        val menuItem = ToolbarMenu.Item.Stop
        browserMenuController.handleMenuInteraction(menuItem)
        Mockito.verify(sessionUseCases, times(1)).stopLoading
    }

    @Test
    @Suppress("MaxLineLength")
    fun `GIVEN RequestDesktop menu item WHEN the item is switched to false THEN requestDesktopCallback with false is called`() {
        val menuItem = ToolbarMenu.Item.RequestDesktop(isChecked = false)
        browserMenuController.handleMenuInteraction(menuItem)
        Mockito.verify(requestDesktopCallback, times(1)).invoke(false)
    }

    @Test
    @Suppress("MaxLineLength")
    fun `GIVEN RequestDesktop menu item WHEN the item is switched to true THEN requestDesktopCallback with true is called`() {
        val menuItem = ToolbarMenu.Item.RequestDesktop(isChecked = true)
        browserMenuController.handleMenuInteraction(menuItem)
        Mockito.verify(requestDesktopCallback, times(1)).invoke(true)
    }

    @Test
    fun `GIVEN OpenInBrowser menu item WHEN the item is pressed THEN openInBrowser is called`() {
        val menuItem = ToolbarMenu.CustomTabItem.OpenInBrowser
        browserMenuController.handleMenuInteraction(menuItem)
        Mockito.verify(openInBrowser, times(1)).invoke()
    }

    @Test
    fun `GIVEN OpenIn menu item WHEN the item is pressed THEN openInCallback is called`() {
        val menuItem = ToolbarMenu.Item.OpenInApp
        browserMenuController.handleMenuInteraction(menuItem)
        Mockito.verify(openInCallback, times(1)).invoke()
    }

    @Test
    fun `GIVEN FindInPage menu item WHEN the item is pressed THEN findInPageMenuEvent method is called`() {
        val menuItem = ToolbarMenu.Item.FindInPage
        browserMenuController.handleMenuInteraction(menuItem)
        Mockito.verify(showFindInPageCallback, times(1)).invoke()
    }

    @Test
    fun `Given AddToShortCut menu item WHEN  the item is pressed THEN addToShortcuts and showShortcutAddedSnackBar are called`() =
        runTest {
            val menuItem = ToolbarMenu.Item.AddToShortcuts
            browserMenuController.handleMenuInteraction(menuItem)

            Mockito.verify(browserMenuController, times(1)).addToShortcuts()
            Mockito.verify(showShortcutAddedSnackBar, times(1)).invoke()
        }
}
