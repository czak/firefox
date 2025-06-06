# -*- Mode: Java; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifdef XP_UNIX
#ifndef XP_MACOSX
#define XP_GNOME 1
#endif
#endif

  <stringbundleset id="stringbundleset">
    <stringbundle id="bundle_brand" src="chrome://branding/locale/brand.properties"/>
  </stringbundleset>

  <!-- The event listeners are defined by _initCommandSet in browser-init.js -->
  <commandset id="mainCommandSet">
    <command id="cmd_newNavigator" />
    <command id="cmd_handleBackspace" />
    <command id="cmd_handleShiftBackspace" />

    <command id="cmd_newNavigatorTab" />
    <command id="cmd_newNavigatorTabNoEvent" />
    <command id="Browser:OpenFile" />
    <command id="Browser:SavePage" />

    <command id="Browser:SendLink" />

    <command id="cmd_pageSetup" />
    <command id="cmd_print" />
    <command id="cmd_printPreviewToggle" />
    <command id="cmd_file_importFromAnotherBrowser" />
    <command id="cmd_help_importFromAnotherBrowser" />
    <command id="cmd_close" />
    <command id="cmd_closeWindow" />
    <command id="cmd_minimizeWindow" />
    <command id="cmd_maximizeWindow" />
    <command id="cmd_restoreWindow" />
    <command id="cmd_toggleMute" />
    <command id="cmd_CustomizeToolbars" />
    <command id="cmd_toggleOfflineStatus" />
    <command id="cmd_quitApplication" />

    <command id="View:AboutProcesses" />
    <command id="View:PageSource" />
    <command id="View:PageInfo" />
    <command id="View:FullScreen" />
    <command id="View:ReaderView" />
    <command id="View:PictureInPicture" />
    <command id="cmd_find" />
    <command id="cmd_findAgain" />
    <command id="cmd_findPrevious" />
#ifdef XP_MACOSX
    <command id="cmd_findSelection" />
#endif
    <command id="cmd_reportBrokenSite"/>
    <command id="cmd_translate" />
    <!-- work-around bug 392512 -->
    <command id="Browser:AddBookmarkAs" />
    <command id="Browser:SearchBookmarks" />
    <command id="Browser:BookmarkAllTabs"/>
    <command id="Browser:Back" disabled="true" />
    <command id="Browser:BackOrBackDuplicate" disabled="true">
      <observes element="Browser:Back" attribute="disabled"/>
    </command>
    <command id="Browser:Forward" disabled="true" />
    <command id="Browser:ForwardOrForwardDuplicate" disabled="true">
      <observes element="Browser:Forward" attribute="disabled"/>
    </command>
    <command id="Browser:Stop" disabled="true" />
    <command id="Browser:Reload" disabled="true" />
    <command id="Browser:ReloadOrDuplicate" disabled="true">
      <observes element="Browser:Reload" attribute="disabled"/>
    </command>
    <command id="Browser:ReloadSkipCache" disabled="true">
      <observes element="Browser:Reload" attribute="disabled"/>
    </command>
    <command id="Profiles:CreateProfile" />
    <command id="Profiles:ManageProfiles" />
    <command id="Profiles:LaunchProfile" />
    <command id="Browser:NextTab" />
    <command id="Browser:PrevTab" />
    <command id="Browser:ShowAllTabs" />
    <command id="cmd_fullZoomReduce" />
    <command id="cmd_fullZoomEnlarge" />
    <command id="cmd_fullZoomReset" />
    <command id="cmd_fullZoomToggle" />
    <command id="cmd_gestureRotateLeft" />
    <command id="cmd_gestureRotateRight" />
    <command id="cmd_gestureRotateEnd" />
    <command id="Browser:OpenLocation" />
    <command id="Browser:RestoreLastSession" disabled="true"/>
    <command id="Browser:NewUserContextTab" />
    <command id="Browser:OpenAboutContainers" />
    <command id="Tools:Search" />
    <command id="Tools:Downloads" />
    <command id="Tools:Addons" />
    <command id="cmd_openUnifiedExtensionsPanel" />
    <command id="Tools:Sanitize" />
    <command id="Tools:PrivateBrowsing" />
    <command id="Browser:Screenshot" />
    <command id="History:UndoCloseTab" data-l10n-args='{"tabCount": 1}' />
    <command id="History:UndoCloseWindow" />
    <command id="History:RestoreLastClosedTabOrWindowOrSession" />
    <command id="History:SearchHistory" />

    <command id="wrCaptureCmd" disabled="true"/>
    <command id="wrToggleCaptureSequenceCmd" disabled="true"/>
#ifdef NIGHTLY_BUILD
    <command id="windowRecordingCmd" />
#endif
#ifdef XP_MACOSX
    <command id="zoomWindow" data-l10n-id="window-zoom-command" />
#endif
  </commandset>
#include ../../components/places/content/placesCommands.inc.xhtml

  <keyset id="mainKeyset">
    <key id="key_newNavigator"
         data-l10n-id="window-new-shortcut"
         command="cmd_newNavigator"
         modifiers="accel" reserved="true"/>
    <key id="key_newNavigatorTab" data-l10n-id="tab-new-shortcut" modifiers="accel"
         command="cmd_newNavigatorTabNoEvent" reserved="true"/>
    <key id="focusURLBar" data-l10n-id="location-open-shortcut" command="Browser:OpenLocation"
         modifiers="accel"/>
#ifndef XP_MACOSX
    <key id="focusURLBar2" data-l10n-id="location-open-shortcut-alt" command="Browser:OpenLocation"
         modifiers="alt"/>
#endif

#
# Search Command Key Logic works like this:
#
# Unix: Ctrl+K (cross platform binding)
#       Ctrl+J (in case of emacs Ctrl-K conflict)
# Mac:  Cmd+K (cross platform binding)
#       Cmd+Opt+F (platform convention)
# Win:  Ctrl+K (cross platform binding)
#       Ctrl+E (IE compat)
#
# We support Ctrl+K on all platforms now and advertise it in the menu since it is
# our standard - it is a "safe" choice since it is near no harmful keys like "W" as
# "E" is. People mourning the loss of Ctrl+K for emacs compat can switch their GTK
# system setting to use emacs emulation, and we should respect it. Focus-Search-Box
# is a fundamental keybinding and we are maintaining a XP binding so that it is easy
# for people to switch to Linux.
#
    <key id="key_search" data-l10n-id="search-focus-shortcut" command="Tools:Search" modifiers="accel"/>
    <key id="key_search2"
#ifdef XP_MACOSX
         data-l10n-id="find-shortcut"
         modifiers="accel,alt"
#else
         data-l10n-id="search-focus-shortcut-alt"
         modifiers="accel"
#endif
         command="Tools:Search"/>
    <key id="key_openDownloads"
         data-l10n-id="downloads-shortcut"
#ifdef XP_GNOME
         modifiers="accel,shift"
#else
         modifiers="accel"
#endif
         command="Tools:Downloads"/>
    <key id="key_openAddons" data-l10n-id="addons-shortcut" command="Tools:Addons" modifiers="accel,shift"/>
    <key id="openFileKb" data-l10n-id="file-open-shortcut" command="Browser:OpenFile"  modifiers="accel"/>
    <key id="key_savePage" data-l10n-id="save-page-shortcut" command="Browser:SavePage" modifiers="accel"/>
    <key id="printKb" data-l10n-id="print-shortcut" command="cmd_print"  modifiers="accel"/>
    <key id="key_close" data-l10n-id="close-shortcut" command="cmd_close" modifiers="accel" reserved="true"/>
    <key id="key_closeWindow" data-l10n-id="close-shortcut" command="cmd_closeWindow" modifiers="accel,shift" reserved="true"/>
    <key id="key_toggleMute" data-l10n-id="mute-toggle-shortcut" command="cmd_toggleMute" modifiers="control"/>
    <key id="key_undo"
         data-l10n-id="text-action-undo-shortcut"
         modifiers="control"
         internal="true"/>
    <key id="key_redo"
#ifdef XP_UNIX
         data-l10n-id="text-action-undo-shortcut"
         modifiers="control,shift"
#else
         data-l10n-id="text-action-redo-shortcut"
         modifiers="accel"
#endif
         internal="true"/>
    <key id="key_cut"
         data-l10n-id="text-action-cut-shortcut"
         modifiers="control"
         internal="true"/>
    <key id="key_copy"
         data-l10n-id="text-action-copy-shortcut"
         modifiers="control"
         internal="true"/>
    <key id="key_paste"
         data-l10n-id="text-action-paste-shortcut"
         modifiers="control"
         internal="true"/>
    <key id="key_delete" keycode="VK_DELETE" command="cmd_delete" reserved="false"/>
    <key id="key_selectAll" data-l10n-id="text-action-select-all-shortcut" modifiers="accel" internal="true"/>

    <key keycode="VK_BACK" command="cmd_handleBackspace" reserved="false"/>
    <key keycode="VK_BACK" command="cmd_handleShiftBackspace" modifiers="shift" reserved="false"/>
#ifndef XP_MACOSX
    <key id="goBackKb"  keycode="VK_LEFT" command="Browser:Back" modifiers="alt"/>
    <key id="goForwardKb"  keycode="VK_RIGHT" command="Browser:Forward" modifiers="alt"/>
#else
    <key id="goBackKb" keycode="VK_LEFT" command="Browser:Back" modifiers="accel" />
    <key id="goForwardKb" keycode="VK_RIGHT" command="Browser:Forward" modifiers="accel" />
#endif
#ifdef XP_UNIX
    <key id="goBackKb2" data-l10n-id="nav-back-shortcut-alt" command="Browser:Back" modifiers="accel"/>
    <key id="goForwardKb2" data-l10n-id="nav-fwd-shortcut-alt" command="Browser:Forward" modifiers="accel"/>
#endif
    <key id="goHome" keycode="VK_HOME" modifiers="alt"/>
    <key keycode="VK_F5" command="Browser:Reload"/>
#ifndef XP_MACOSX
    <key id="showAllHistoryKb" data-l10n-id="history-show-all-shortcut" command="Browser:ShowAllHistory" modifiers="accel,shift"/>
    <key keycode="VK_F5" command="Browser:ReloadSkipCache" modifiers="accel"/>
    <key id="key_enterFullScreen" keycode="VK_F11" command="View:FullScreen"/>
    <key id="key_exitFullScreen" keycode="VK_F11" command="View:FullScreen" reserved="true" disabled="true"/>
#else
    <key id="showAllHistoryKb" data-l10n-id="history-show-all-shortcut-mac" command="Browser:ShowAllHistory" modifiers="accel"/>
    <key id="key_enterFullScreen" data-l10n-id="full-screen-shortcut" command="View:FullScreen" modifiers="accel,control"/>
    <key id="key_enterFullScreen_old" data-l10n-id="full-screen-shortcut" command="View:FullScreen" modifiers="accel,shift"/>
    <key id="key_enterFullScreen_compat" keycode="VK_F11" command="View:FullScreen"/>
    <key id="key_exitFullScreen" data-l10n-id="full-screen-shortcut" command="View:FullScreen" modifiers="accel,control" reserved="true" disabled="true"/>
    <key id="key_exitFullScreen_old" data-l10n-id="full-screen-shortcut" command="View:FullScreen" modifiers="accel,shift" reserved="true" disabled="true"/>
    <key id="key_exitFullScreen_compat" keycode="VK_F11" command="View:FullScreen" reserved="true" disabled="true"/>
#endif

#ifndef XP_MACOSX
    <key id="key_togglePictureInPicture" data-l10n-id="picture-in-picture-toggle-shortcut" command="View:PictureInPicture" modifiers="accel,shift"/>
    <key                                 data-l10n-id="picture-in-picture-toggle-shortcut-alt" command="View:PictureInPicture" modifiers="accel,shift"/>
#else
    <key id="key_togglePictureInPicture" data-l10n-id="picture-in-picture-toggle-shortcut-mac" command="View:PictureInPicture" modifiers="accel,alt,shift"/>
    <key                                 data-l10n-id="picture-in-picture-toggle-shortcut-mac-alt" command="View:PictureInPicture" modifiers="accel,alt,shift"/>
#endif

    <key data-l10n-id="nav-reload-shortcut" command="Browser:Reload" modifiers="accel" id="key_reload"/>
    <key data-l10n-id="nav-reload-shortcut" command="Browser:ReloadSkipCache" modifiers="accel,shift" id="key_reload_skip_cache"/>
    <key id="key_aboutProcesses" command="View:AboutProcesses" keycode="VK_ESCAPE" modifiers="shift"/>
    <key id="key_viewSource" data-l10n-id="page-source-shortcut" command="View:PageSource" modifiers="accel"/>
#ifdef XP_MACOSX
    <key id="key_viewSourceSafari" data-l10n-id="page-source-shortcut-safari" command="View:PageSource" modifiers="accel,alt"/>
#endif
    <key id="key_viewInfo" data-l10n-id="page-info-shortcut" command="View:PageInfo"   modifiers="accel"/>
    <key id="key_find" data-l10n-id="find-shortcut" command="cmd_find" modifiers="accel"/>
    <key id="key_findAgain" data-l10n-id="search-find-again-shortcut" command="cmd_findAgain" modifiers="accel"/>
    <key id="key_findPrevious" data-l10n-id="search-find-again-shortcut" command="cmd_findPrevious" modifiers="accel,shift"/>
#ifdef XP_MACOSX
    <key id="key_findSelection" data-l10n-id="search-find-selection-shortcut" command="cmd_findSelection" modifiers="accel"/>
#endif
    <key data-l10n-id="search-find-again-shortcut-alt" command="cmd_findAgain"/>
    <key data-l10n-id="search-find-again-shortcut-alt" command="cmd_findPrevious" modifiers="shift"/>

    <key id="addBookmarkAsKb" data-l10n-id="bookmark-this-page-shortcut" command="Browser:AddBookmarkAs" modifiers="accel"/>
    <key id="bookmarkAllTabsKb"
         data-l10n-id="bookmark-this-page-shortcut"
         modifiers="accel,shift"/>
    <key id="manBookmarkKb" data-l10n-id="bookmark-show-library-shortcut" command="Browser:ShowAllBookmarks" modifiers="accel,shift"/>
    <key id="viewBookmarksSidebarKb"
         data-l10n-id="bookmark-show-sidebar-shortcut"
         modifiers="accel"/>
    <key id="viewBookmarksToolbarKb"
         data-l10n-id="bookmark-show-toolbar-shortcut"
         modifiers="accel,shift"/>

    <key id="key_stop" keycode="VK_ESCAPE" command="Browser:Stop"/>

#ifdef XP_MACOSX
    <key id="key_stop_mac" modifiers="accel" data-l10n-id="nav-stop-shortcut" command="Browser:Stop"/>
#endif

    <key id="viewGenaiChatSidebarKb"
         data-l10n-id="ai-chatbot-sidebar-shortcut"
#ifdef XP_MACOSX
         modifiers="control"
#else
         modifiers="accel,alt"
#endif
         />
    <key id="toggleSidebarKb"
         data-l10n-id="toggle-sidebar-shortcut"
#ifdef XP_MACOSX
         modifiers="control"
#else
         modifiers="control,alt"
#endif
        />
    <key id="key_gotoHistory"
         data-l10n-id="history-sidebar-shortcut"
#ifdef XP_MACOSX
         modifiers="accel,shift"
#else
         modifiers="accel"
#endif
        />

    <key id="key_fullZoomReduce" data-l10n-id="full-zoom-reduce-shortcut" command="cmd_fullZoomReduce"  modifiers="accel"/>
    <key                         data-l10n-id="full-zoom-reduce-shortcut-alt-a" command="cmd_fullZoomReduce"  modifiers="accel"/>
    <key                         data-l10n-id="full-zoom-reduce-shortcut-alt-b" command="cmd_fullZoomReduce"  modifiers="accel"/>
    <key id="key_fullZoomEnlarge" data-l10n-id="full-zoom-enlarge-shortcut" command="cmd_fullZoomEnlarge" modifiers="accel"/>
    <key                          data-l10n-id="full-zoom-enlarge-shortcut-alt" command="cmd_fullZoomEnlarge" modifiers="accel"/>
    <key                          data-l10n-id="full-zoom-enlarge-shortcut-alt2" command="cmd_fullZoomEnlarge" modifiers="accel"/>
    <key id="key_fullZoomReset"   data-l10n-id="full-zoom-reset-shortcut"    command="cmd_fullZoomReset"   modifiers="accel"/>
    <key                          data-l10n-id="full-zoom-reset-shortcut-alt"   command="cmd_fullZoomReset"   modifiers="accel"/>

    <key id="key_showAllTabs" keycode="VK_TAB" modifiers="control,shift"/>

    <key id="key_switchTextDirection" data-l10n-id="bidi-switch-direction-shortcut" command="cmd_switchTextDirection" modifiers="accel,shift" />

    <key id="key_privatebrowsing" command="Tools:PrivateBrowsing" data-l10n-id="private-browsing-shortcut"
         modifiers="accel,shift" reserved="true"/>
    <key id="key_screenshot" data-l10n-id="screenshot-shortcut" command="Browser:Screenshot" modifiers="accel,shift"/>
    <key id="key_sanitize" command="Tools:Sanitize" keycode="VK_DELETE" modifiers="accel,shift"/>
#ifdef XP_MACOSX
    <key id="key_sanitize_mac" command="Tools:Sanitize" keycode="VK_BACK" modifiers="accel,shift"/>
#endif
    <key id="key_quitApplication" data-l10n-id="quit-app-shortcut"
#ifdef XP_WIN
         modifiers="accel,shift"
#else
         modifiers="accel"
#endif
# On OS X, dark voodoo magic invokes the quit code for this key.
# So we're not adding the attribute on OSX because of backwards/add-on compat.
# See bug 1369909 for background on this.
#ifdef XP_MACOSX
         internal="true"
#else
         command="cmd_quitApplication"
#endif
         reserved="true"/>

    <key id="key_restoreLastClosedTabOrWindowOrSession" command="History:RestoreLastClosedTabOrWindowOrSession" data-l10n-id="tab-new-shortcut" modifiers="accel,shift"/>
    <key id="key_undoCloseWindow" command="History:UndoCloseWindow" data-l10n-id="window-new-shortcut" modifiers="accel,shift"/>

#ifdef XP_GNOME
#define NUM_SELECT_TAB_MODIFIER alt
#else
#define NUM_SELECT_TAB_MODIFIER accel
#endif

#expand    <key id="key_selectTab1" key="1" modifiers="__NUM_SELECT_TAB_MODIFIER__"/>
#expand    <key id="key_selectTab2" key="2" modifiers="__NUM_SELECT_TAB_MODIFIER__"/>
#expand    <key id="key_selectTab3" key="3" modifiers="__NUM_SELECT_TAB_MODIFIER__"/>
#expand    <key id="key_selectTab4" key="4" modifiers="__NUM_SELECT_TAB_MODIFIER__"/>
#expand    <key id="key_selectTab5" key="5" modifiers="__NUM_SELECT_TAB_MODIFIER__"/>
#expand    <key id="key_selectTab6" key="6" modifiers="__NUM_SELECT_TAB_MODIFIER__"/>
#expand    <key id="key_selectTab7" key="7" modifiers="__NUM_SELECT_TAB_MODIFIER__"/>
#expand    <key id="key_selectTab8" key="8" modifiers="__NUM_SELECT_TAB_MODIFIER__"/>
#expand    <key id="key_selectLastTab" key="9" modifiers="__NUM_SELECT_TAB_MODIFIER__"/>

    <key id="key_wrCaptureCmd"
#ifdef XP_MACOSX
    key="3" modifiers="control,shift"
#else
    key="#" modifiers="control"
#endif
    command="wrCaptureCmd"/>
    <key id="key_wrToggleCaptureSequenceCmd"
#ifdef XP_MACOSX
    key="6" modifiers="control,shift"
#else
    key="^" modifiers="control"
#endif
    command="wrToggleCaptureSequenceCmd"/>
#ifdef NIGHTLY_BUILD
    <key id="key_windowRecordingCmd"
#ifdef XP_MACOSX
    key="4" modifiers="control,shift"
#else
    key="$" modifiers="control"
#endif
    command="windowRecordingCmd"/>
#endif
#ifdef XP_MACOSX
    <key id="key_minimizeWindow"
         command="cmd_minimizeWindow"
         data-l10n-id="window-minimize-shortcut"
         modifiers="accel"
         internal="true"/>
    <!-- These are used to build the Application menu -->
    <key id="key_preferencesCmdMac"
         data-l10n-id="preferences-shortcut"
         modifiers="accel"
         internal="true"/>
    <key id="key_hideThisAppCmdMac"
         data-l10n-id="hide-app-shortcut"
         modifiers="accel"
         internal="true"/>
    <key id="key_hideOtherAppsCmdMac"
         data-l10n-id="hide-other-apps-shortcut"
         modifiers="accel,alt"
         internal="true"/>
#endif
  </keyset>
