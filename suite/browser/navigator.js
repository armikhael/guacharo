/* -*- Mode: Java; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Blake Ross <blakeross@telocity.com>
 *   Peter Annema <disttsc@bart.nl>
 *   Dean Tessman <dean_tessman@hotmail.com>
 *   Nils Maier <maierman@web.de>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

const REMOTESERVICE_CONTRACTID = "@mozilla.org/toolkit/remote-service;1";
const XUL_NAMESPACE = "http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul";
var gURLBar = null;
var gProxyButton = null;
var gProxyFavIcon = null;
var gProxyDeck = null;
var gBookmarksService = null;
var gSearchService = null;
var gNavigatorBundle;
var gBrandBundle;
var gNavigatorRegionBundle;
var gLastValidURLStr = "";
var gLastValidURL = null;
var gClickSelectsAll = false;
var gClickAtEndSelects = false;
var gIgnoreFocus = false;
var gIgnoreClick = false;
var gURIFixup = null;

//cached elements
var gBrowser = null;

// Pref listener constants
const gButtonPrefListener =
{
  domain: "browser.toolbars.showbutton",
  init: function()
  {
    var array = Services.prefs.getChildList(this.domain, {});
    array.forEach(
      function(item) {
        if (/\.(bookmarks|home|print)$/.test(item))
          Services.prefs.clearUserPref(item);
        else
          this.updateButton(item);
      }, this
    )
  },
  observe: function(subject, topic, prefName)
  {
    // verify that we're changing a button pref
    if (topic != "nsPref:changed")
      return;

    this.updateButton(prefName);
  },
  updateButton: function(prefName)
  {
    var buttonName = prefName.substr(this.domain.length+1);
    var buttonId = buttonName + "-button";
    var button = document.getElementById(buttonId);
    if (button)
      button.hidden = !Services.prefs.getBoolPref(prefName);
  }
};

const gTabStripPrefListener =
{
  domain: "browser.tabs.autoHide",
  observe: function(subject, topic, prefName)
  {
    // verify that we're changing the tab browser strip auto hide pref
    if (topic != "nsPref:changed")
      return;

    if (gBrowser.tabContainer.childNodes.length == 1 && window.toolbar.visible) {
      var stripVisibility = !Services.prefs.getBoolPref(prefName);
      gBrowser.setStripVisibilityTo(stripVisibility);
      Services.prefs.setBoolPref("browser.tabs.forceHide", false);
    }
  }
};

const gHomepagePrefListener =
{
  domain: "browser.startup.homepage",
  observe: function(subject, topic, prefName)
  {
    // verify that we're changing the home page pref
    if (topic != "nsPref:changed")
      return;

    updateHomeButtonTooltip();
  }
};

const gStatusBarPopupIconPrefListener =
{
  domain: "privacy.popups.statusbar_icon_enabled",
  observe: function(subject, topic, prefName)
  {
    if (topic != "nsPref:changed" || prefName != this.domain)
      return;

    var popupIcon = document.getElementById("popupIcon");
    if (!Services.prefs.getBoolPref(prefName))
      popupIcon.hidden = true;

    else if (gBrowser.getNotificationBox().popupCount)
      popupIcon.hidden = false;
  }
};

// popup window permission change listener
const gPopupPermListener = {

  observe: function(subject, topic, data) {
    if (topic == "popup-perm-close") {
      // close the window if we're a popup and our opener's URI matches
      // the URI in the notification
      var popupOpenerURI = maybeInitPopupContext();
      if (popupOpenerURI) {
        closeURI = Services.io.newURI(data, null, null);
        if (closeURI.host == popupOpenerURI.host)
          window.close();
      }
    }
  }
};

/**
* Pref listener handler functions.
* Both functions assume that observer.domain is set to 
* the pref domain we want to start/stop listening to.
*/
function addPrefListener(observer)
{
  try {
    Services.prefs.addObserver(observer.domain, observer, false);
  } catch(ex) {
    dump("Failed to observe prefs: " + ex + "\n");
  }
}

function removePrefListener(observer)
{
  try {
    Services.prefs.removeObserver(observer.domain, observer);
  } catch(ex) {
    dump("Failed to remove pref observer: " + ex + "\n");
  }
}

function addPopupPermListener(observer)
{
  Services.obs.addObserver(observer, "popup-perm-close", false);
}

function removePopupPermListener(observer)
{
  Services.obs.removeObserver(observer, "popup-perm-close");
}

/**
* We can avoid adding multiple load event listeners and save some time by adding
* one listener that calls all real handlers.
*/

function pageShowEventHandlers(event)
{
  // Filter out events that are not about the document load we are interested in
  if (event.originalTarget == content.document) {
    UpdateBookmarksLastVisitedDate(event);
    UpdateInternetSearchResults(event);
    checkForDirectoryListing();
    postURLToNativeWidget();
  }
}

/**
 * Determine whether or not the content area is displaying a page with frames,
 * and if so, toggle the display of the 'save frame as' menu item.
 **/
function getContentAreaFrameCount()
{
  var saveFrameItem = document.getElementById("saveframe");
  if (!content || !content.frames.length || !isContentFrame(document.commandDispatcher.focusedWindow))
    saveFrameItem.setAttribute("hidden", "true");
  else {
    var autoDownload = Services.prefs.getBoolPref("browser.download.useDownloadDir");
    goSetMenuValue("saveframe", autoDownload ? "valueSave" : "valueSaveAs");
    saveFrameItem.removeAttribute("hidden");
  }
}

function saveFrameDocument()
{
  var focusedWindow = document.commandDispatcher.focusedWindow;
  if (isContentFrame(focusedWindow))
    saveDocument(focusedWindow.document, true);
}

function updateHomeButtonTooltip()
{
  var homePage = getHomePage();
  var tooltip = document.getElementById("home-button-tooltip-inner");

  while (tooltip.firstChild)
    tooltip.removeChild(tooltip.firstChild);

  for (var i in homePage) {
    var label = document.createElementNS(XUL_NAMESPACE, "label");
    label.setAttribute("value", homePage[i]);
    tooltip.appendChild(label);
  }
}

//////////////////////////////// BOOKMARKS ////////////////////////////////////

function UpdateBookmarksLastVisitedDate(event)
{
  var url = getWebNavigation().currentURI.spec;
  if (url) {
    // if the URL is bookmarked, update its "Last Visited" date
    if (!gBookmarksService)
      gBookmarksService = Components.classes["@mozilla.org/browser/bookmarks-service;1"]
                                    .getService(Components.interfaces.nsIBookmarksService);

    gBookmarksService.updateLastVisitedDate(url, content.document.characterSet);
  }
}

function HandleBookmarkIcon(iconURL, addFlag)
{
  var url = content.document.documentURI;
  if (url) {
    // update URL with new icon reference
    if (!gBookmarksService)
      gBookmarksService = Components.classes["@mozilla.org/browser/bookmarks-service;1"]
                                    .getService(Components.interfaces.nsIBookmarksService);
    if (addFlag)    gBookmarksService.updateBookmarkIcon(url, iconURL);
    else            gBookmarksService.removeBookmarkIcon(url, iconURL);
  }
}

function UpdateInternetSearchResults(event)
{
  var url = getWebNavigation().currentURI.spec;
  if (url && isSearchPanelOpen())
  {
    if (!gSearchService)
      gSearchService = Components.classes["@mozilla.org/rdf/datasource;1?name=internetsearch"]
                                 .getService(Components.interfaces.nsIInternetSearchService);

    gSearchService.FindInternetSearchResults(url);
  }
}

function getBrowser()
{
  if (!gBrowser)
    gBrowser = document.getElementById("content");
  return gBrowser;
}

function getHomePage()
{
  var URIs = [];
  try {
    URIs[0] = Services.prefs.getComplexValue("browser.startup.homepage",
                                             nsIPrefLocalizedString).data;
    var count = Services.prefs.getIntPref("browser.startup.homepage.count");
    for (var i = 1; i < count; ++i) {
      URIs[i] = Services.prefs.getComplexValue("browser.startup.homepage." + i,
                                               nsIPrefLocalizedString).data;
    }
  } catch(e) {
  }

  return URIs;
}

function UpdateBackForwardButtons()
{
  var backBroadcaster = document.getElementById("canGoBack");
  var forwardBroadcaster = document.getElementById("canGoForward");
  var upBroadcaster = document.getElementById("canGoUp");
  var browser = getBrowser();

  // Avoid setting attributes on broadcasters if the value hasn't changed!
  // Remember, guys, setting attributes on elements is expensive!  They
  // get inherited into anonymous content, broadcast to other widgets, etc.!
  // Don't do it if the value hasn't changed! - dwh

  var backDisabled = backBroadcaster.hasAttribute("disabled");
  var forwardDisabled = forwardBroadcaster.hasAttribute("disabled");
  var upDisabled = upBroadcaster.hasAttribute("disabled");
  if (backDisabled == browser.canGoBack) {
    if (backDisabled)
      backBroadcaster.removeAttribute("disabled");
    else
      backBroadcaster.setAttribute("disabled", true);
  }
  if (forwardDisabled == browser.canGoForward) {
    if (forwardDisabled)
      forwardBroadcaster.removeAttribute("disabled");
    else
      forwardBroadcaster.setAttribute("disabled", true);
  }
  if (upDisabled != !browser.currentURI.spec.replace(/[#?].*$/, "").match(/\/[^\/]+\/./)) {
    if (upDisabled)
      upBroadcaster.removeAttribute("disabled");
    else
      upBroadcaster.setAttribute("disabled", true);
  }
}

const nsIBrowserDOMWindow = Components.interfaces.nsIBrowserDOMWindow;
const nsIInterfaceRequestor = Components.interfaces.nsIInterfaceRequestor;

function nsBrowserAccess() {
}

nsBrowserAccess.prototype = {
  openURI: function openURI(aURI, aOpener, aWhere, aContext) {
    var loadflags = aContext == nsIBrowserDOMWindow.OPEN_EXTERNAL ?
                    nsIWebNavigation.LOAD_FLAGS_FROM_EXTERNAL :
                    nsIWebNavigation.LOAD_FLAGS_NONE;

    if (aWhere == nsIBrowserDOMWindow.OPEN_DEFAULTWINDOW)
      if (aContext == nsIBrowserDOMWindow.OPEN_EXTERNAL)
        aWhere = Services.prefs.getIntPref("browser.link.open_external");
      else
        aWhere = Services.prefs.getIntPref("browser.link.open_newwindow");
    var referrer = aOpener ? aOpener.QueryInterface(nsIInterfaceRequestor)
                                    .getInterface(nsIWebNavigation)
                                    .currentURI : null;
    switch (aWhere) {
      case nsIBrowserDOMWindow.OPEN_NEWWINDOW:
        var uri = aURI ? aURI.spec : "about:blank";
        return window.openDialog(getBrowserURL(), "_blank", "all,dialog=no",
                                 uri, null, referrer);
      case nsIBrowserDOMWindow.OPEN_NEWTAB:
        var newTab = gBrowser.addTab("about:blank", null, null,
            !Services.prefs.getBoolPref("browser.tabs.loadDivertedInBackground"));
        var browser = gBrowser.getBrowserForTab(newTab);
        if (aURI) {
          try {
            browser.loadURIWithFlags(aURI.spec, loadflags, referrer);
          } catch (e) {}
        }
        return browser.contentWindow;
      default:
        if (!aOpener) {
          if (aURI)
            gBrowser.loadURIWithFlags(aURI.spec, loadflags);
          return content;
        }
        aOpener = aOpener.top;
        if (aURI) {
          try {
            aOpener.QueryInterface(nsIInterfaceRequestor)
                   .getInterface(nsIWebNavigation)
                   .loadURI(uri, loadflags, referrer, null, null);
          } catch (e) {}
        }
        return aOpener;
    }
  },
  isTabContentWindow: function isTabContentWindow(aWindow) {
    var browsers = gBrowser.browsers;
    for (var i = 0; browsers.item(i); i++)
      if (browsers[i].contentWindow == aWindow)
        return true;
    return false;
  }
}

function HandleAppCommandEvent(aEvent)
{
  aEvent.stopPropagation();
  switch (aEvent.command) {
    case "Back":
      BrowserBack();
      break;
    case "Forward":
      BrowserForward();
      break;
    case "Reload":
      BrowserReloadSkipCache();
      break;
    case "Stop":
      BrowserStop();
      break;
    case "Search":
      BrowserSearchInternet();
      break;
    case "Bookmarks":
      toBookmarksManager();
      break;
    case "Home":
      BrowserHome(null);
      break;
    default:
      break;
  }
}

function Startup()
{
  // init globals
  gNavigatorBundle = document.getElementById("bundle_navigator");
  gBrandBundle = document.getElementById("bundle_brand");
  gNavigatorRegionBundle = document.getElementById("bundle_navigator_region");

  gBrowser = document.getElementById("content");
  gURLBar = document.getElementById("urlbar");
  
  SetPageProxyState("invalid", null);

  var webNavigation;
  try {
    webNavigation = getWebNavigation();
    if (!webNavigation)
      throw "no XBL binding for browser";
  } catch (e) {
    alert("Error launching browser window:" + e);
    window.close(); // Give up.
    return;
  }

  // Do all UI building here:

  // Ensure button visibility matches prefs
  gButtonPrefListener.init();

  // set home button tooltip text
  updateHomeButtonTooltip();

  // initialize observers and listeners
  window.XULBrowserWindow = new nsBrowserStatusHandler();
  window.QueryInterface(Components.interfaces.nsIInterfaceRequestor)
        .getInterface(Components.interfaces.nsIWebNavigation)
        .QueryInterface(Components.interfaces.nsIDocShellTreeItem).treeOwner
        .QueryInterface(Components.interfaces.nsIInterfaceRequestor)
        .getInterface(Components.interfaces.nsIXULWindow)
        .XULBrowserWindow = window.XULBrowserWindow;

  addPrefListener(gButtonPrefListener); 
  addPrefListener(gTabStripPrefListener);
  addPrefListener(gHomepagePrefListener);
  addPrefListener(gStatusBarPopupIconPrefListener);
  addPopupPermListener(gPopupPermListener);

  window.browserContentListener =
    new nsBrowserContentListener(window, getBrowser());
  
  // Add a capturing event listener to the content area
  // (rjc note: not the entire window, otherwise we'll get sidebar pane loads too!)
  //  so we'll be notified when onloads complete.
  var contentArea = document.getElementById("appcontent");
  contentArea.addEventListener("pageshow", pageShowEventHandlers, true);

  // set default character set if provided
  if ("arguments" in window && window.arguments.length > 1 && window.arguments[1]) {
    if (window.arguments[1].indexOf("charset=") != -1) {
      var arrayArgComponents = window.arguments[1].split("=");
      if (arrayArgComponents) {
        //we should "inherit" the charset menu setting in a new window
        getMarkupDocumentViewer().defaultCharacterSet = arrayArgComponents[1];
      }
    }
  }

  //initConsoleListener();

  // Set a sane starting width/height for all resolutions on new profiles.
  if (!document.documentElement.hasAttribute("width")) {
    var defaultHeight = screen.availHeight;
    var defaultWidth= screen.availWidth;

    // Create a narrower window for large or wide-aspect displays, to suggest
    // side-by-side page view.
    if (screen.availWidth >= 1440)
      defaultWidth /= 2;

    // Tweak sizes to be sure we don't grow outside the screen
    defaultWidth = defaultWidth - 20;
    defaultHeight = defaultHeight - 10;

    // On X, we're not currently able to account for the size of the window
    // border.  Use 28px as a guess (titlebar + bottom window border)
    if (navigator.appVersion.indexOf("X11") != -1)
      defaultHeight -= 28;

    // On small screens, default to maximized state
    if (defaultHeight <= 600)
      document.documentElement.setAttribute("sizemode", "maximized");

    document.documentElement.setAttribute("width", defaultWidth);
    document.documentElement.setAttribute("height", defaultHeight);
    // Make sure we're safe at the left/top edge of screen
    document.documentElement.setAttribute("screenX", screen.availLeft);
    document.documentElement.setAttribute("screenY", screen.availTop);
  }

  // hook up UI through progress listener
  getBrowser().addProgressListener(window.XULBrowserWindow, Components.interfaces.nsIWebProgress.NOTIFY_ALL);

  var uriToLoad = "";

  // Check window.arguments[0]. If not null then use it for uriArray
  // otherwise the new window is being called when another browser
  // window already exists so use the New Window pref for uriArray
  if ("arguments" in window && window.arguments.length >= 1) {
    var uriArray;
    if (window.arguments[0]) {
      uriArray = window.arguments[0].toString().split('\n'); // stringify and split
    } else {
      try {
        switch (Services.prefs.getIntPref("browser.windows.loadOnNewWindow"))
        {
          default:
            uriArray = ["about:blank"];
            break;
          case 1:
            uriArray = getHomePage();
            break;
          case 2:
            var history = Components.classes["@mozilla.org/browser/global-history;2"]
                                    .getService(Components.interfaces.nsIBrowserHistory);
            uriArray = [history.lastPageVisited];
            break;
        }
      } catch(e) {
        uriArray = ["about:blank"];
      }
    }
    uriToLoad = uriArray.splice(0, 1)[0];

    if (uriArray.length > 0)
      window.setTimeout(function(arg) { for (var i in arg) gBrowser.addTab(arg[i]); }, 0, uriArray);
  }
    
  if (/^\s*$/.test(uriToLoad))
    uriToLoad = "about:blank";

  var browser = getBrowser();

  if (uriToLoad != "about:blank") {
    gURLBar.value = uriToLoad;
    browser.userTypedValue = uriToLoad;
    if ("arguments" in window && window.arguments.length >= 4) {
      loadURI(uriToLoad, window.arguments[2], window.arguments[3]);
    } else if ("arguments" in window && window.arguments.length == 3) {
      loadURI(uriToLoad, window.arguments[2]);
    } else {
      loadURI(uriToLoad);
    }
  }

  // Focus the content area unless we're loading a blank page, or if
  // we weren't passed any arguments. This "breaks" the
  // javascript:window.open(); case where we don't get any arguments
  // either, but we're loading about:blank, but focusing the content
  // are is arguably correct in that case as well since the opener
  // is very likely to put some content in the new window, and then
  // the focus should be in the content area.
  var navBar = document.getElementById("nav-bar");
  if ("arguments" in window && uriToLoad == "about:blank" && isElementVisible(gURLBar))
    setTimeout(WindowFocusTimerCallback, 0, gURLBar);
  else
    setTimeout(WindowFocusTimerCallback, 0, content);

  // Perform default browser checking (after window opens).
  setTimeout( checkForDefaultBrowser, 0 );

  // hook up browser access support
  window.browserDOMWindow = new nsBrowserAccess();

  // hook up remote support
  if (REMOTESERVICE_CONTRACTID in Components.classes) {
    var remoteService =
      Components.classes[REMOTESERVICE_CONTRACTID]
                .getService(Components.interfaces.nsIRemoteService);
    remoteService.registerWindow(window);
  }

  // ensure login manager is loaded
  Components.classes["@mozilla.org/login-manager;1"].getService();
  
  // called when we go into full screen, even if it is 
  // initiated by a web page script
  addEventListener("fullscreen", onFullScreen, true);

  addEventListener("PopupCountChanged", UpdateStatusBarPopupIcon, true);

  addEventListener("AppCommand", HandleAppCommandEvent, true);

  // does clicking on the urlbar select its contents?
  gClickSelectsAll = Services.prefs.getBoolPref("browser.urlbar.clickSelectsAll");
  gClickAtEndSelects = Services.prefs.getBoolPref("browser.urlbar.clickAtEndSelects");

  // BiDi UI
  gShowBiDi = isBidiEnabled();
  if (gShowBiDi) {
    document.getElementById("documentDirection-swap").hidden = false;
    document.getElementById("textfieldDirection-separator").hidden = false;
    document.getElementById("textfieldDirection-swap").hidden = false;
  }

  // Before and after callbacks for the customizeToolbar code
  getNavToolbox().customizeInit = BrowserToolboxCustomizeInit;
  getNavToolbox().customizeDone = BrowserToolboxCustomizeDone;
  getNavToolbox().customizeChange = BrowserToolboxCustomizeChange;

  // now load bookmarks after a delay
  setTimeout(LoadBookmarksCallback, 0);

  // initialize the session-restore service
  setTimeout(InitSessionStoreCallback, 0);
}

function LoadBookmarksCallback()
{
  // loads the services
  initServices();
  initBMService();
  BMSVC.readBookmarks();
  var bt = document.getElementById("bookmarks-ptf");
  if (bt) {
    bt.database.AddObserver(BookmarksToolbarRDFObserver);
  }
  window.addEventListener("resize", BookmarksToolbar.resizeFunc, false);
  controllers.appendController(BookmarksMenuController);
}

function InitSessionStoreCallback()
{
  try {
    var ss = Components.classes["@mozilla.org/suite/sessionstore;1"]
                       .getService(Components.interfaces.nsISessionStore);
    ss.init(window);
  } catch(ex) {
    dump("nsSessionStore could not be initialized: " + ex + "\n");
  }
}

function WindowFocusTimerCallback(element)
{
  // This fuction is a redo of the fix for jag bug 91884
  if (window == Services.ww.activeWindow) {
    element.focus();
  } else {
    // set the element in command dispatcher so focus will restore properly
    // when the window does become active

    if (element instanceof Components.interfaces.nsIDOMWindow) {
      document.commandDispatcher.focusedWindow = element;
      document.commandDispatcher.focusedElement = null;
    } else if (element instanceof Components.interfaces.nsIDOMElement) {
      document.commandDispatcher.focusedWindow = element.ownerDocument.defaultView;
      document.commandDispatcher.focusedElement = element;
    }
  }
}

function BrowserFlushBookmarks()
{
  // Flush bookmarks (used when window closes or is cached).
  try {
    // If bookmarks are dirty, flush 'em to disk
    var bmks = Components.classes["@mozilla.org/browser/bookmarks-service;1"]
                         .getService(Components.interfaces.nsIRDFRemoteDataSource);
    bmks.Flush();
  } catch(ex) {
  }
}

function Shutdown()
{
  // shut down browser access support
  window.browserDOMWindow = null;

  try {
    getBrowser().removeProgressListener(window.XULBrowserWindow);
  } catch (ex) {
    // Perhaps we didn't get around to adding the progress listener
  }

  try {
    controllers.removeController(BookmarksMenuController);
  } catch (ex) {
    // Perhaps we didn't get around to adding the controller
  }

  var bt = document.getElementById("bookmarks-ptf");
  if (bt) {
    bt.database.RemoveObserver(BookmarksToolbarRDFObserver);
  }

  // remove the extension manager RDF datasource to prevent 'leaking' it
  // see bug 391318, the real cause of reporting leaks is probably bug 406914
  var extDB = document.getElementById('menu_ViewApplyTheme_Popup').database;
  var extDS = extDB.GetDataSources();
  while (extDS.hasMoreElements())
    extDB.RemoveDataSource(extDS.getNext());

  window.XULBrowserWindow.destroy();
  window.XULBrowserWindow = null;
  window.QueryInterface(Components.interfaces.nsIInterfaceRequestor)
        .getInterface(Components.interfaces.nsIWebNavigation)
        .QueryInterface(Components.interfaces.nsIDocShellTreeItem).treeOwner
        .QueryInterface(Components.interfaces.nsIInterfaceRequestor)
        .getInterface(Components.interfaces.nsIXULWindow)
        .XULBrowserWindow = null;

  BrowserFlushBookmarks();

  // unregister us as a pref listener
  removePrefListener(gButtonPrefListener);
  removePrefListener(gTabStripPrefListener);
  removePrefListener(gHomepagePrefListener);
  removePrefListener(gStatusBarPopupIconPrefListener);
  removePopupPermListener(gPopupPermListener);

  window.browserContentListener.close();
}

function Translate()
{
  var service = Services.prefs.getComplexValue("browser.translation.service",
                                               nsIPrefLocalizedString).data;
  var serviceDomain = Services.prefs.getComplexValue("browser.translation.serviceDomain",
                                                     nsIPrefLocalizedString).data;
  var targetURI = getWebNavigation().currentURI.spec;

  // if we're already viewing a translated page, then just reload
  if (targetURI.indexOf(serviceDomain) >= 0)
    BrowserReload();
  else {
    loadURI(encodeURI(service + targetURI));
  }
}

function gotoHistoryIndex(aEvent)
{
  var index = aEvent.target.getAttribute("index");
  if (!index)
    return false;

  if (index == "back")
    gBrowser.goBackGroup();
  else if (index ==  "forward")
    gBrowser.goForwardGroup();
  else {
    try {
      getWebNavigation().gotoIndex(index);
    }
    catch(ex) {
      return false;
    }
  }
  return true;

}

function BrowserBack()
{
  try {
    getBrowser().goBack();
  }
  catch(ex) {
  }
}

function BrowserHandleBackspace()
{
  switch (Services.prefs.getIntPref("browser.backspace_action")) {
    case 0:
      BrowserBack();
      break;
    case 1:
      goDoCommand("cmd_scrollPageUp");
      break;
  }
}

function BrowserForward()
{
  try {
    getBrowser().goForward();
  }
  catch(ex) {
  }
}

function BrowserUp()
{
  loadURI(getBrowser().currentURI.spec.replace(/[#?].*$/, "").replace(/\/[^\/]*.$/, "/"));
}

function BrowserHandleShiftBackspace()
{
  switch (Services.prefs.getIntPref("browser.backspace_action")) {
    case 0:
      BrowserForward();
      break;
    case 1:
      goDoCommand("cmd_scrollPageDown");
      break;
  } 
}

function SetGroupHistory(popupMenu, direction)
{
  while (popupMenu.firstChild)
    popupMenu.removeChild(popupMenu.firstChild);

  var menuItem = document.createElementNS(XUL_NAMESPACE, "menuitem");
  var label = gNavigatorBundle.getString("tabs.historyItem");
  menuItem.setAttribute("label", label);
  menuItem.setAttribute("index", direction);
  popupMenu.appendChild(menuItem);
}

function BrowserBackMenu(event)
{
  if (gBrowser.backBrowserGroup.length != 0) {
    SetGroupHistory(event.target, "back");
    return true;
  }

  return FillHistoryMenu(event.target, "back");
}

function BrowserForwardMenu(event)
{
  if (gBrowser.forwardBrowserGroup.length != 0) {
    SetGroupHistory(event.target, "forward");
    return true;
  }

  return FillHistoryMenu(event.target, "forward");
}

function BrowserStop()
{
  try {
    const stopFlags = nsIWebNavigation.STOP_ALL;
    getWebNavigation().stop(stopFlags);
  }
  catch(ex) {
  }
}

function BrowserReload()
{
  const reloadFlags = nsIWebNavigation.LOAD_FLAGS_NONE;
  return BrowserReloadWithFlags(reloadFlags);
}

function BrowserReloadSkipCache()
{
  // Bypass proxy and cache.
  const reloadFlags = nsIWebNavigation.LOAD_FLAGS_BYPASS_PROXY | nsIWebNavigation.LOAD_FLAGS_BYPASS_CACHE;
  return BrowserReloadWithFlags(reloadFlags);
}

function BrowserHome(aEvent)
{
  var tab;
  var homePage = getHomePage();
  var target = !gBrowser ? "window": !aEvent ? "current" : BookmarksUtils.getBrowserTargetFromEvent(aEvent);

  if (homePage.length == 1) {
    switch (target) {
    case "current":
      loadURI(homePage[0]);
      break;
    case "tab":
      tab = gBrowser.addTab(homePage[0]);
      if (!BookmarksUtils.shouldLoadTabInBackground(aEvent))
        gBrowser.selectedTab = tab;
      break;
    case "window":
      openDialog(getBrowserURL(), "_blank", "chrome,all,dialog=no", homePage[0]);
    }
  } else {
    if (target == "window")
      openDialog(getBrowserURL(), "_blank", "chrome,all,dialog=no", homePage.join("\n"));
    else {
      var URIs = [];
      for (var i in homePage)
        URIs.push({URI: homePage[i]});

      tab = gBrowser.loadGroup(URIs);
      
      if (!BookmarksUtils.shouldLoadTabInBackground(aEvent))
        gBrowser.selectedTab = tab;
    }
  }
}

function addBookmarkAs()
{
  const browsers = gBrowser.browsers;
  if (browsers.length > 1)
    BookmarksUtils.addBookmarkForTabBrowser(gBrowser);
  else
    BookmarksUtils.addBookmarkForBrowser(gBrowser.webNavigation, true);
}

function addGroupmarkAs()
{
  BookmarksUtils.addBookmarkForTabBrowser(gBrowser, true);
}

function updateGroupmarkCommand()
{
  const disabled = (!gBrowser || gBrowser.browsers.length == 1);
  document.getElementById("Browser:AddGroupmarkAs")
          .setAttribute("disabled", disabled);
}

function readRDFString(aDS,aRes,aProp)
{
  var n = aDS.GetTarget(aRes, aProp, true);
  return n ? n.QueryInterface(Components.interfaces.nsIRDFLiteral).Value : "";
}

function ensureDefaultEnginePrefs(aRDF,aDS) 
{
  var defaultName = Services.prefs.getComplexValue("browser.search.defaultenginename",
                                                   nsIPrefLocalizedString).data;
  var kNC_Root = aRDF.GetResource("NC:SearchEngineRoot");
  var kNC_child = aRDF.GetResource("http://home.netscape.com/NC-rdf#child");
  var kNC_Name = aRDF.GetResource("http://home.netscape.com/NC-rdf#Name");

  var arcs = aDS.GetTargets(kNC_Root, kNC_child, true);
  while (arcs.hasMoreElements()) {
    var engineRes = arcs.getNext().QueryInterface(Components.interfaces.nsIRDFResource);
    var name = readRDFString(aDS, engineRes, kNC_Name);
    if (name == defaultName)
      mPrefs.setCharPref("browser.search.defaultengine", engineRes.Value);
  }
}

function ensureSearchPref()
{
  var rdf = Components.classes["@mozilla.org/rdf/rdf-service;1"]
                      .getService(Components.interfaces.nsIRDFService);
  var ds = rdf.GetDataSource("rdf:internetsearch");
  var kNC_Name = rdf.GetResource("http://home.netscape.com/NC-rdf#Name");
  var defaultEngine;
  try {
    defaultEngine = Services.prefs.getCharPref("browser.search.defaultengine");
  } catch(ex) {
    ensureDefaultEnginePrefs(rdf, ds);
    defaultEngine = Services.prefs.getCharPref("browser.search.defaultengine");
  }
}

function getSearchUrl(attr)
{
  var rdf = Components.classes["@mozilla.org/rdf/rdf-service;1"]
                      .getService(Components.interfaces.nsIRDFService);
  var ds = rdf.GetDataSource("rdf:internetsearch");
  var kNC_Root = rdf.GetResource("NC:SearchEngineRoot");
  var defaultEngine = Services.prefs.getCharPref("browser.search.defaultengine");
  var engineRes = rdf.GetResource(defaultEngine);
  var prop = "http://home.netscape.com/NC-rdf#" + attr;
  var kNC_attr = rdf.GetResource(prop);
  var searchURL = readRDFString(ds, engineRes, kNC_attr);
  return searchURL;
}

function QualifySearchTerm()
{
  // If the text in the URL bar is the same as the currently loaded
  // page's URL then treat this as an empty search term.  This way
  // the user is taken to the search page where s/he can enter a term.
  if (gBrowser.userTypedValue !== null)
    return gURLBar.value;
  return "";
}

function OpenSearch(tabName, searchStr, newWindowOrTabFlag, reverseBackgroundPref)
{
  //This function needs to be split up someday.

  var autoOpenSearchPanel = false;
  var defaultSearchURL = null;
  var fallbackDefaultSearchURL = gNavigatorRegionBundle.getString("fallbackDefaultSearchURL");
  ensureSearchPref()
  //Check to see if search string contains "://" or "ftp." or white space.
  //If it does treat as url and match for pattern

  var urlmatch= /(:\/\/|^ftp\.)[^ \S]+$/ 
  var forceAsURL = urlmatch.test(searchStr);

  try {
    autoOpenSearchPanel = Services.prefs.getBoolPref("browser.search.opensidebarsearchpanel");
    defaultSearchURL = Services.prefs.getComplexValue("browser.search.defaulturl",
                                                      nsIPrefLocalizedString).data;
  } catch (ex) {
  }

  // Fallback to a default url (one that we can get sidebar search results for)
  if (!defaultSearchURL)
    defaultSearchURL = fallbackDefaultSearchURL;

  if (!searchStr) {
    BrowserSearchInternet();
  } else {

    //Check to see if location bar field is a url
    //If it is a url go to URL.  A Url is "://" or "." as commented above
    //Otherwise search on entry
    if (forceAsURL) {
       BrowserLoadURL()
    } else {
      if (searchStr) {
        var escapedSearchStr = encodeURIComponent(searchStr);
        defaultSearchURL += escapedSearchStr;
        var searchDS = Components.classes["@mozilla.org/rdf/datasource;1?name=internetsearch"]
                                 .getService(Components.interfaces.nsIInternetSearchService);

        searchDS.RememberLastSearchText(escapedSearchStr);
        try {
          var searchEngineURI = Services.prefs.getCharPref("browser.search.defaultengine");
          if (searchEngineURI) {
            var searchURL = getSearchUrl("actionButton");
            if (searchURL) {
              defaultSearchURL = searchURL + escapedSearchStr; 
            } else {
              searchURL = searchDS.GetInternetSearchURL(searchEngineURI, escapedSearchStr, 0, 0, {value:0});
              if (searchURL)
                defaultSearchURL = searchURL;
            }
          }
        } catch (ex) {
        }

        if (!newWindowOrTabFlag)
          loadURI(defaultSearchURL);
        else if (!Services.prefs.getBoolPref("browser.search.opentabforcontextsearch"))
          window.open(defaultSearchURL, "_blank");
        else {
          var newTab = gBrowser.addTab(defaultSearchURL);
          var loadInBackground = Services.prefs.getBoolPref("browser.tabs.loadInBackground");
          if (reverseBackgroundPref)
            loadInBackground = !loadInBackground;
          if (!loadInBackground)
            gBrowser.selectedTab = newTab;
        }
      }
    }
  }

  // should we try and open up the sidebar to show the "Search Results" panel?
  if (autoOpenSearchPanel)
    RevealSearchPanel();
}

function RevealSearchPanel()
{
  // first lets check if the search panel will be shown at all
  // by checking the sidebar datasource to see if there is an entry
  // for the search panel, and if it is excluded for navigator or not
  
  var searchPanelExists = false;
  
  if (document.getElementById("urn:sidebar:panel:search")) {
    var myPanel = document.getElementById("urn:sidebar:panel:search");
    var panel = sidebarObj.panels.get_panel_from_header_node(myPanel);

    searchPanelExists = !panel.is_excluded();
  } else if (sidebarObj.never_built) {

    try{
      var datasource = RDF.GetDataSourceBlocking(sidebarObj.datasource_uri);
      var aboutValue = RDF.GetResource("urn:sidebar:panel:search");

      // check if the panel is even in the list by checking for its content
      var contentProp = RDF.GetResource("http://home.netscape.com/NC-rdf#content");
      var content = datasource.GetTarget(aboutValue, contentProp, true);
     
      if (content instanceof Components.interfaces.nsIRDFLiteral){
        // the search panel entry exists, now check if it is excluded
        // for navigator
        var excludeProp = RDF.GetResource("http://home.netscape.com/NC-rdf#exclude");
        var exclude = datasource.GetTarget(aboutValue, excludeProp, true);

        if (exclude instanceof Components.interfaces.nsIRDFLiteral) {
          searchPanelExists = (exclude.Value.indexOf("navigator:browser") < 0);
        } else {
          // panel exists and no exclude set
          searchPanelExists = true;
        }
      }
    } catch(e){
      searchPanelExists = false;
    }
  }

  if (searchPanelExists) {
    // make sure the sidebar is open, else SidebarSelectPanel() will fail
    if (sidebar_is_hidden())
      SidebarShowHide();
  
    if (sidebar_is_collapsed())
      SidebarExpandCollapse();

    var searchPanel = document.getElementById("urn:sidebar:panel:search");
    if (searchPanel)
      SidebarSelectPanel(searchPanel, true, true); // lives in sidebarOverlay.js      
  }
}

function isSearchPanelOpen()
{
  return ( !sidebar_is_hidden()    && 
           !sidebar_is_collapsed() && 
           SidebarGetLastSelectedPanel() == "urn:sidebar:panel:search"
         );
}

function BrowserSearchInternet()
{
  try {
    var searchEngineURI = Services.prefs.getCharPref("browser.search.defaultengine");
    if (searchEngineURI) {
      var searchRoot = getSearchUrl("searchForm");
      if (searchRoot) {
        openTopWin(searchRoot);
        return;
      } else {
        // Get a search URL and guess that the front page of the site has a search form.
        var searchDS = Components.classes["@mozilla.org/rdf/datasource;1?name=internetsearch"]
                                 .getService(Components.interfaces.nsIInternetSearchService);
        var searchURL = searchDS.GetInternetSearchURL(searchEngineURI, "ABC", 0, 0, {value:0});
        if (searchURL) {
          searchRoot = searchURL.match(/^[^:]+:\/\/[^?/]+/i);
          if (searchRoot) {
            openTopWin(searchRoot + "/");
            return;
          }
        }
      }
    }
  } catch (ex) {
  }

  // Fallback if the stuff above fails: use the hard-coded search engine
  openTopWin(gNavigatorRegionBundle.getString("otherSearchURL"));
}


//Note: BrowserNewEditorWindow() was moved to globalOverlay.xul and renamed to NewEditorWindow()

function BrowserOpenWindow()
{
  //opens a window where users can select a web location to open
  var params = { browser: window, action: null, url: "" };
  openDialog("chrome://communicator/content/openLocation.xul", "_blank", "chrome,modal,titlebar", params);
  var url = getShortcutOrURI(params.url);
  switch (params.action) {
    case "0": // current window
      loadURI(url, null, nsIWebNavigation.LOAD_FLAGS_ALLOW_THIRD_PARTY_FIXUP);
      break;
    case "1": // new window
      openDialog(getBrowserURL(), "_blank", "all,dialog=no", url, null, null,
                 nsIWebNavigation.LOAD_FLAGS_ALLOW_THIRD_PARTY_FIXUP);
      break;
    case "2": // edit
      editPage(url);
      break;
    case "3": // new tab
      gBrowser.selectedTab = gBrowser.addTab(url, null, null, false,
               nsIWebNavigation.LOAD_FLAGS_ALLOW_THIRD_PARTY_FIXUP);
      break;
  }
}

function BrowserOpenTab()
{
  if (!gInPrintPreviewMode) {
    var uriToLoad;
    try {
      switch ( Services.prefs.getIntPref("browser.tabs.loadOnNewTab") )
      {
        default:
          uriToLoad = "about:blank";
          break;
        case 1:
          uriToLoad = Services.prefs.getComplexValue("browser.startup.homepage",
                                                     nsIPrefLocalizedString).data;
          break;
        case 2:
          uriToLoad = gBrowser ? getWebNavigation().currentURI.spec
                               : Components.classes["@mozilla.org/browser/global-history;2"]
                                           .getService(Components.interfaces.nsIBrowserHistory)
                                           .lastPageVisited;
          break;
      }
    } catch(e) {
      uriToLoad = "about:blank";
    }

    // Open a new window if someone requests a new tab when no browser window is open
    if (!gBrowser) {
      openDialog(getBrowserURL(), "_blank", "chrome,all,dialog=no", uriToLoad);
      return;
    }

    gBrowser.selectedTab = gBrowser.addTab(uriToLoad);
    if (uriToLoad == "about:blank" && isElementVisible(gURLBar))
      setTimeout("gURLBar.focus();", 0);
    else
      setTimeout("content.focus();", 0);
  }
}

/* Show file picker dialog configured for opening a file, and return 
 * the selected nsIFileURL instance. */
function selectFileToOpen(label, prefRoot)
{
  var fileURL = null;

  // Get filepicker component.
  const nsIFilePicker = Components.interfaces.nsIFilePicker;
  var fp = Components.classes["@mozilla.org/filepicker;1"].createInstance(nsIFilePicker);
  fp.init(window, gNavigatorBundle.getString(label), nsIFilePicker.modeOpen);
  fp.appendFilters(nsIFilePicker.filterAll | nsIFilePicker.filterText | nsIFilePicker.filterImages |
                   nsIFilePicker.filterXML | nsIFilePicker.filterHTML);

  const filterIndexPref = prefRoot + "filterIndex";
  const lastDirPref = prefRoot + "dir";

  // use a pref to remember the filterIndex selected by the user.
  var index = 0;
  try {
    index = Services.prefs.getIntPref(filterIndexPref);
  } catch (ex) {
  }
  fp.filterIndex = index;

  // use a pref to remember the displayDirectory selected by the user.
  try {
    fp.displayDirectory = Services.prefs.getComplexValue(lastDirPref,
                              Components.interfaces.nsILocalFile);
  } catch (ex) {
  }

  if (fp.show() == nsIFilePicker.returnOK) {
    Services.prefs.setIntPref(filterIndexPref, fp.filterIndex);
    Services.prefs.setComplexValue(lastDirPref,
                                   Components.interfaces.nsILocalFile,
                                   fp.file.parent);
    fileURL = fp.fileURL;
  }

  return fileURL;
}

function BrowserOpenFileWindow()
{
  try {
    openTopWin(selectFileToOpen("openFile", "browser.open.").spec);
  } catch (e) {}
}

function updateCloseItems()
{
  var browser = getBrowser();
  var ss = Components.classes["@mozilla.org/suite/sessionstore;1"]
                     .getService(Components.interfaces.nsISessionStore);

  var hideCloseWindow = Services.prefs.getBoolPref("browser.tabs.closeWindowWithLastTab") &&
                        (!browser || browser.tabContainer.childNodes.length <= 1);
  document.getElementById("menu_closeWindow").hidden = hideCloseWindow;
  var closeItem = document.getElementById("menu_close");
  if (hideCloseWindow) {
    closeItem.setAttribute("label", gNavigatorBundle.getString("tabs.close.label"));
    closeItem.setAttribute("accesskey", gNavigatorBundle.getString("tabs.close.accesskey"));
  } else {
    closeItem.setAttribute("label", gNavigatorBundle.getString("tabs.closeTab.label"));
    closeItem.setAttribute("accesskey", gNavigatorBundle.getString("tabs.closeTab.accesskey"));
  }

  var hideCloseOtherTabs = !browser || !browser.getStripVisibility();
  document.getElementById("menu_closeOtherTabs").hidden = hideCloseOtherTabs;
  if (!hideCloseOtherTabs)
    document.getElementById("cmd_closeOtherTabs").setAttribute("disabled", hideCloseWindow);

  var recentTabsItem = document.getElementById("menu_recentTabs");
  recentTabsItem.setAttribute("disabled", !browser || browser.getUndoList().length == 0);
  var recentWindowsItem = document.getElementById("menu_recentWindows");
  recentWindowsItem.setAttribute("disabled", ss.getClosedWindowCount() == 0);
}

function updateRecentTabs(menupopup)
{
  var browser = getBrowser();

  while (menupopup.hasChildNodes())
    menupopup.removeChild(menupopup.lastChild);

  var list = browser.getUndoList();
  for (var i = 0; i < list.length; i++) {
    var menuitem = document.createElement("menuitem");
    var label = list[i];
    if (i < 9) {
      label = gNavigatorBundle.getFormattedString("tabs.recentlyClosed.format", [i + 1, label]);
      menuitem.setAttribute("accesskey", i + 1);
    }

    if (i == 0)
      menuitem.setAttribute("key", "key_restoreTab");

    menuitem.setAttribute("label", label);
    menuitem.setAttribute("value", i);
    menupopup.appendChild(menuitem);
  }
}

function updateRecentWindows(menupopup)
{
  var ss = Components.classes["@mozilla.org/suite/sessionstore;1"]
                     .getService(Components.interfaces.nsISessionStore);

  while (menupopup.hasChildNodes())
    menupopup.removeChild(menupopup.lastChild);

  var undoItems = JSON.parse(ss.getClosedWindowData());
  for (var i = 0; i < undoItems.length; i++) {
    var menuitem = document.createElement("menuitem");
    var label = undoItems[i].title;
    if (i < 9) {
      label = gNavigatorBundle.getFormattedString("windows.recentlyClosed.format", [i + 1, label]);
      menuitem.setAttribute("accesskey", i + 1);
    }

    if (i == 0)
      menuitem.setAttribute("key", "key_restoreWindow");

    menuitem.setAttribute("label", label);
    menuitem.setAttribute("value", i);
    menupopup.appendChild(menuitem);
  }
}

function undoCloseWindow(aIndex)
{
  var ss = Components.classes["@mozilla.org/suite/sessionstore;1"]
                     .getService(Components.interfaces.nsISessionStore);

  return ss.undoCloseWindow(aIndex);
}

function BrowserCloseOtherTabs()
{
  var browser = getBrowser();
  browser.removeAllTabsBut(browser.mCurrentTab);
}

function BrowserCloseTabOrWindow()
{
  var browser = getBrowser();
  if (browser.tabContainer.childNodes.length > 1 ||
      !Services.prefs.getBoolPref("browser.tabs.closeWindowWithLastTab")) {
    // Just close up a tab.
    browser.removeCurrentTab();
    return;
  }

  BrowserCloseWindow();
}

function BrowserTryToCloseWindow()
{
  if (WindowIsClosing())
    BrowserCloseWindow();
}

function BrowserCloseWindow() 
{
  // This code replicates stuff in Shutdown().  It is here because
  // window.screenX and window.screenY have real values.  We need
  // to fix this eventually but by replicating the code here, we
  // provide a means of saving position (it just requires that the
  // user close the window via File->Close (vs. close box).
  
  // Get the current window position/size.
  var x = window.screenX;
  var y = window.screenY;
  var h = window.outerHeight;
  var w = window.outerWidth;

  // Store these into the window attributes (for persistence).
  var win = document.getElementById( "main-window" );
  win.setAttribute( "x", x );
  win.setAttribute( "y", y );
  win.setAttribute( "height", h );
  win.setAttribute( "width", w );

  window.close();
}

function loadURI(uri, referrer, flags)
{
  try {
    getBrowser().loadURIWithFlags(uri, flags, referrer);
  } catch (e) {
  }
}

function BrowserLoadURL(aTriggeringEvent)
{
  // Remove leading and trailing spaces first
  var url = gURLBar.value.trim();

  if (url.match(/^view-source:/)) {
    BrowserViewSourceOfURL(url.replace(/^view-source:/, ""), null, null);
  } else {
    // Check the pressed modifiers: (also see bug 97123)
    // Modifier Mac | Modifier PC | Action
    // -------------+-------------+-----------
    // Command      | Control     | New Window/Tab
    // Shift+Cmd    | Shift+Ctrl  | New Window/Tab behind current one
    // Option       | Shift       | Save URL (show Filepicker)

    // If false, the save modifier is Alt, which is Option on Mac.
    var modifierIsShift = true;
    try {
      modifierIsShift = Services.prefs.getBoolPref("ui.key.saveLink.shift");
    }
    catch (ex) {}

    var shiftPressed = false;
    var saveModifier = false; // if the save modifier was pressed
    if (aTriggeringEvent && 'shiftKey' in aTriggeringEvent &&
        'altKey' in aTriggeringEvent) {
      saveModifier = modifierIsShift ? aTriggeringEvent.shiftKey
                     : aTriggeringEvent.altKey;
      shiftPressed = aTriggeringEvent.shiftKey;
    }

    var browser = getBrowser();
    url = getShortcutOrURI(url);
    // Accept both Control and Meta (=Command) as New-Window-Modifiers
    if (aTriggeringEvent &&
        (('ctrlKey' in aTriggeringEvent && aTriggeringEvent.ctrlKey) ||
         ('metaKey' in aTriggeringEvent && aTriggeringEvent.metaKey))) {
      // Check if user requests Tabs instead of windows
      var openTab = false;
      try {
        openTab = Services.prefs.getBoolPref("browser.tabs.opentabfor.urlbar");
      }
      catch (ex) {}

      if (openTab) {
        // Open link in new tab
        var t = browser.addTab(url, null, null, false,
                        nsIWebNavigation.LOAD_FLAGS_ALLOW_THIRD_PARTY_FIXUP);

        // Focus new tab unless shift is pressed
        if (!shiftPressed) {
          browser.userTypedValue = null;
          browser.selectedTab = t;
        }
      } else {
        // Open a new window with the URL
        var newWin = openDialog(getBrowserURL(), "_blank", "all,dialog=no", url,
            null, null, nsIWebNavigation.LOAD_FLAGS_ALLOW_THIRD_PARTY_FIXUP);
        // Reset url in the urlbar
        URLBarSetURI();

        // Focus old window if shift was pressed, as there's no
        // way to open a new window in the background
        // XXX this doesn't seem to work
        if (shiftPressed) {
          //newWin.blur();
          content.focus();
        }
      }
    } else if (saveModifier) {
      try {
        // Firstly, fixup the url so that (e.g.) "www.foo.com" works
        const nsIURIFixup = Components.interfaces.nsIURIFixup;
        if (!gURIFixup)
          gURIFixup = Components.classes["@mozilla.org/docshell/urifixup;1"]
                                .getService(nsIURIFixup);
        url = gURIFixup.createFixupURI(url, nsIURIFixup.FIXUP_FLAGS_MAKE_ALTERNATE_URI).spec;
        // Open filepicker to save the url
        saveURL(url, null, null, false, true);
      }
      catch(ex) {
        // XXX Do nothing for now.
        // Do we want to put up an alert in the future?  Mmm, l10n...
      }
    } else {
      // No modifier was pressed, load the URL normally and
      // focus the content area
      loadURI(url, null, nsIWebNavigation.LOAD_FLAGS_ALLOW_THIRD_PARTY_FIXUP);
      content.focus();
    }
  }
}

function getShortcutOrURI(url)
{
  // rjc: added support for URL shortcuts (3/30/1999)
  try {
    if (!gBookmarksService)
      gBookmarksService = Components.classes["@mozilla.org/browser/bookmarks-service;1"]
                                    .getService(Components.interfaces.nsIBookmarksService);

    var shortcutURL = gBookmarksService.resolveKeyword(url);
    if (!shortcutURL) {
      // rjc: add support for string substitution with shortcuts (4/4/2000)
      //      (see bug # 29871 for details)
      var aOffset = url.indexOf(" ");
      if (aOffset > 0) {
        var cmd = url.substr(0, aOffset);
        var text = url.substr(aOffset+1);
        shortcutURL = gBookmarksService.resolveKeyword(cmd);
        // Bug 123006 : %s replace and URI escape, %S replace with raw value
        if (shortcutURL && text) {
          var encodedText = null; 
          var charset = "";
          const re = /^(.*)\&mozcharset=([a-zA-Z][_\-a-zA-Z0-9]+)\s*$/; 
          var matches = shortcutURL.match(re);
          if (matches) {
             shortcutURL = matches[1];
             charset = matches[2];
          }
          else if (/%s/.test(shortcutURL)) {
            try {
              charset = BMSVC.getLastCharset(shortcutURL);
            } catch (ex) {
            }
          }

          if (charset)
            encodedText = escape(convertFromUnicode(charset, text)); 
          else  // default case: charset=UTF-8
            encodedText = encodeURIComponent(text);

          if (encodedText && /%[sS]/.test(shortcutURL))
            shortcutURL = shortcutURL.replace(/%s/g, encodedText)
                                     .replace(/%S/g, text);
          else 
            shortcutURL = null;
        }
      }
    }

    if (shortcutURL)
      url = shortcutURL;

  } catch (ex) {
  }
  return url;
}

function readFromClipboard()
{
  var url;

  try {
    // Get clipboard.
    var clipboard = Components.classes["@mozilla.org/widget/clipboard;1"]
                              .getService(Components.interfaces.nsIClipboard);

    // Create tranferable that will transfer the text.
    var trans = Components.classes["@mozilla.org/widget/transferable;1"]
                          .createInstance(Components.interfaces.nsITransferable);

    trans.addDataFlavor("text/unicode");
    // If available, use selection clipboard, otherwise global one
    if (clipboard.supportsSelectionClipboard())
      clipboard.getData(trans, clipboard.kSelectionClipboard);
    else
      clipboard.getData(trans, clipboard.kGlobalClipboard);

    var data = {};
    var dataLen = {};
    trans.getTransferData("text/unicode", data, dataLen);

    if (data) {
      data = data.value.QueryInterface(Components.interfaces.nsISupportsString);
      url = data.data.substring(0, dataLen.value / 2);
    }
  } catch (ex) {
  }

  return url;
}

function BrowserViewSourceOfDocument(aDocument)
{
  var docCharset;
  var pageCookie;
  var webNav;

  // Get the document charset
  docCharset = "charset=" + aDocument.characterSet;

  // Get the nsIWebNavigation associated with the document
  try {
      var win;
      var ifRequestor;

      // Get the DOMWindow for the requested document.  If the DOMWindow
      // cannot be found, then just use the content window...
      //
      // XXX:  This is a bit of a hack...
      win = aDocument.defaultView;
      if (win == window) {
        win = content;
      }
      ifRequestor = win.QueryInterface(Components.interfaces.nsIInterfaceRequestor);

      webNav = ifRequestor.getInterface(Components.interfaces.nsIWebNavigation);
  } catch(err) {
      // If nsIWebNavigation cannot be found, just get the one for the whole
      // window...
      webNav = getWebNavigation();
  }
  //
  // Get the 'PageDescriptor' for the current document. This allows the
  // view-source to access the cached copy of the content rather than
  // refetching it from the network...
  //
  try{
    var PageLoader = webNav.QueryInterface(Components.interfaces.nsIWebPageDescriptor);

    pageCookie = PageLoader.currentDescriptor;
  } catch(err) {
    // If no page descriptor is available, just use the view-source URL...
  }

  BrowserViewSourceOfURL(webNav.currentURI.spec, docCharset, pageCookie);
}

function BrowserViewSourceOfURL(url, charset, pageCookie)
{
  // try to open a view-source window while inheriting the charset (if any)
  openDialog("chrome://navigator/content/viewSource.xul",
             "_blank",
             "all,dialog=no",
             url, charset, pageCookie);
}

// doc - document to use for source, or null for the current tab
// initialTab - id of the initial tab to display, or null for the first tab
function BrowserPageInfo(doc, initialTab)
{
  if (!doc)
    doc = window.content.document;
  var relatedUrl = doc.location.toString();
  var args = {doc: doc, initialTab: initialTab};

  var enumerator = Services.wm.getEnumerator("Browser:page-info");
  // Check for windows matching the url
  while (enumerator.hasMoreElements()) {
    let win = enumerator.getNext();
    if (win.document.documentElement
           .getAttribute("relatedUrl") == relatedUrl) {
      win.focus();
      win.resetPageInfo(args);
      return win;
    }
  }
  // We didn't find a matching window, so open a new one.
  return window.openDialog("chrome://navigator/content/pageinfo/pageInfo.xul",
                           "_blank",
                           "chrome,dialog=no",
                           args);
}

function hiddenWindowStartup()
{
  // focus the hidden window
  window.focus();

  // Disable menus which are not appropriate
  var disabledItems = ['cmd_close', 'Browser:SendPage',
                       'Browser:EditPage', 'Browser:SavePage', 'cmd_printSetup',
                       'Browser:Print', 'canGoBack', 'canGoForward',
                       'Browser:AddBookmark', 'Browser:AddBookmarkAs',
                       'cmd_undo', 'cmd_redo', 'cmd_cut', 'cmd_copy',
                       'cmd_paste', 'cmd_delete', 'cmd_selectAll',
                       'cmd_findTypeText', 'cmd_findTypeLinks', 'Browser:Find',
                       'Browser:FindAgain', 'Browser:FindPrev', 'menu_Toolbars',
                       'menuitem_reload', 'menu_UseStyleSheet', 'charsetMenu',
                       'View:PageSource', 'View:PageInfo', 'menu_translate',
                       'BlockCookies', 'UseCookiesDefault',
                       'AllowSessionCookies', 'AllowCookies', 'BlockImages',
                       'UseImagesDefault', 'AllowImages', 'AllowPopups',
                       'menu_zoom', 'cmd_minimizeWindow', 'cmd_zoomWindow'];
  var broadcaster;

  for (var id in disabledItems) {
    broadcaster = document.getElementById(disabledItems[id]);
    if (broadcaster)
      broadcaster.setAttribute("disabled", "true");
  }

  // also hide the window list separator
  var separator = document.getElementById("sep-window-list");
  if (separator)
    separator.setAttribute("hidden", "true");

  // init string bundles
  gNavigatorBundle = document.getElementById("bundle_navigator");
  gNavigatorRegionBundle = document.getElementById("bundle_navigator_region");
  gBrandBundle = document.getElementById("bundle_brand");

  // now load bookmarks after a delay
  setTimeout(hiddenWindowLoadBookmarksCallback, 0);
}

function hiddenWindowLoadBookmarksCallback()
{
  // loads the services
  initServices();
  initBMService();
  BMSVC.readBookmarks();  
}

var consoleListener = {
  observe: function (aMsgObject)
  {
    const nsIScriptError = Components.interfaces.nsIScriptError;
    var scriptError = aMsgObject.QueryInterface(nsIScriptError);
    var isWarning = scriptError.flags & nsIScriptError.warningFlag != 0;
    if (!isWarning) {
      var statusbarDisplay = document.getElementById("statusbar-display");
      statusbarDisplay.setAttribute("error", "true");
      statusbarDisplay.addEventListener("click", loadErrorConsole, true);
      statusbarDisplay.label = gNavigatorBundle.getString("jserror");
      this.isShowingError = true;
    }
  },

  // whether or not an error alert is being displayed
  isShowingError: false
};

function initConsoleListener()
{
  /**
   * XXX - console launch hookup requires some work that I'm not sure
   * how to do.
   *
   *       1) ideally, the notification would disappear when the
   *       document that had the error was flushed. how do I know when
   *       this happens? All the nsIScriptError object I get tells me
   *       is the URL. Where is it located in the content area?
   *       2) the notification service should not display chrome
   *       script errors.  web developers and users are not interested
   *       in the failings of our shitty, exception unsafe js. One
   *       could argue that this should also extend to the console by
   *       default (although toggle-able via setting for chrome
   *       authors) At any rate, no status indication should be given
   *       for chrome script errors.
   *
   *       As a result I am commenting out this for the moment.
   *

  var consoleService = Components.classes["@mozilla.org/consoleservice;1"]
                                 .getService(Components.interfaces.nsIConsoleService);

  if (consoleService)
    consoleService.registerListener(consoleListener);
  */
}

function loadErrorConsole(aEvent)
{
  if (aEvent.detail == 2)
    toJavaScriptConsole();
}

function clearErrorNotification()
{
  var statusbarDisplay = document.getElementById("statusbar-display");
  statusbarDisplay.removeAttribute("error");
  statusbarDisplay.removeEventListener("click", loadErrorConsole, true);
  consoleListener.isShowingError = false;
}

const NS_URLWIDGET_CONTRACTID = "@mozilla.org/urlwidget;1";
var urlWidgetService = null;
if (NS_URLWIDGET_CONTRACTID in Components.classes) {
  urlWidgetService = Components.classes[NS_URLWIDGET_CONTRACTID]
                               .getService(Components.interfaces.nsIUrlWidget);
}

//Posts the currently displayed url to a native widget so third-party apps can observe it.
function postURLToNativeWidget()
{
  if (urlWidgetService) {
    var url = getWebNavigation().currentURI.spec;
    try {
      urlWidgetService.SetURLToHiddenControl(url, window);
    } catch(ex) {
    }
  }
}

function checkForDirectoryListing()
{
  if ( "HTTPIndex" in content &&
       content.HTTPIndex instanceof Components.interfaces.nsIHTTPIndex ) {
    content.defaultCharacterset = getMarkupDocumentViewer().defaultCharacterSet;
  }
}

function URLBarSetURI(aURI, aValid) {
  var uri = aURI || getWebNavigation().currentURI;
  var value;

  // If the url has "wyciwyg://" as the protocol, strip it off.
  // Nobody wants to see it on the urlbar for dynamically generated pages.
  if (!gURIFixup)
    gURIFixup = Components.classes["@mozilla.org/docshell/urifixup;1"]
                          .getService(Components.interfaces.nsIURIFixup);
  try {
    uri = gURIFixup.createExposableURI(uri);
  } catch (ex) {}

  // Replace "about:blank" with an empty string
  // only if there's no opener (bug 370555).
  if (uri.spec == "about:blank")
    value = content.opener || getWebNavigation().canGoBack ? "about:blank" : "";
  else
    value = losslessDecodeURI(uri);

  gURLBar.value = value;
  // In some cases, setting the urlBar value causes userTypedValue to
  // become set because of oninput, so reset it to null.
  getBrowser().userTypedValue = null;

  SetPageProxyState((value && (!aURI || aValid)) ? "valid" : "invalid", uri);
}

function losslessDecodeURI(aURI) {
  var value = aURI.spec;
  // Try to decode as UTF-8 if there's no encoding sequence that we would break.
  if (!/%25(?:3B|2F|3F|3A|40|26|3D|2B|24|2C|23)/i.test(value))
    try {
      value = decodeURI(value)
                // decodeURI decodes %25 to %, which creates unintended
                // encoding sequences. Re-encode it, unless it's part of
                // a sequence that survived decodeURI, i.e. one for:
                // ';', '/', '?', ':', '@', '&', '=', '+', '$', ',', '#'
                // (RFC 3987 section 3.2)
                .replace(/%(?!3B|2F|3F|3A|40|26|3D|2B|24|2C|23)/ig,
                         encodeURIComponent);
    } catch (e) {}

  // Encode invisible characters (soft hyphen, zero-width space, BOM,
  // line and paragraph separator, word joiner, invisible times,
  // invisible separator, object replacement character) (bug 452979)
  // Encode bidirectional formatting characters.
  // (RFC 3987 sections 3.2 and 4.1 paragraph 6)
  // Re-encode whitespace so that it doesn't get eaten away
  // by the location bar (bug 410726).
  return value.replace(/[\x09-\x0d\x1c-\x1f\u00ad\u200b\u200e\u200f\u2028-\u202e\u2060\u2062\u2063\ufeff\ufffc]/g,
                        encodeURIComponent);
}

/**
 * Use Stylesheet functions.
 *     Written by Tim Hill (bug 6782)
 *     Frameset handling by Neil Rashbrook <neil@parkwaycc.co.uk>
 **/
/**
 * Adds this frame's stylesheet sets to the View > Use Style submenu
 *
 * If the frame has no preferred stylesheet set then the "Default style"
 * menuitem should be shown. Note that it defaults to checked, hidden.
 *
 * If this frame has a selected stylesheet set then its menuitem should
 * be checked (unless the "None" style is currently selected), and the
 * "Default style" menuitem should to be unchecked.
 *
 * The stylesheet sets may match those of other frames. In that case, the
 * checkmark should be removed from sets that are not selected in this frame.
 *
 * @param menuPopup          The submenu's popup child
 * @param frame              The frame whose sets are to be added
 * @param styleDisabled      True if the "None" style is currently selected
 * @param itemPersistentOnly The "Default style" menuitem element
 */
function stylesheetFillFrame(menuPopup, frame, styleDisabled, itemPersistentOnly)
{
  if (!frame.document.preferredStyleSheetSet)
    itemPersistentOnly.hidden = false;

  var title = frame.document.selectedStyleSheetSet;
  if (title)
    itemPersistentOnly.removeAttribute("checked");

  var styleSheetSets = frame.document.styleSheetSets;
  for (var i = 0; i < styleSheetSets.length; i++) {
    var styleSheetSet = styleSheetSets[i];
    var menuitem = menuPopup.getElementsByAttribute("data", styleSheetSet).item(0);
    if (menuitem) {
      if (styleSheetSet != title)
        menuitem.removeAttribute("checked");
    } else {
      var menuItem = document.createElement("menuitem");
      menuItem.setAttribute("type", "radio");
      menuItem.setAttribute("label", styleSheetSet);
      menuItem.setAttribute("data", styleSheetSet);
      menuItem.setAttribute("checked", styleSheetSet == title && !styleDisabled);
      menuPopup.appendChild(menuItem);
    }
  }
}
/**
 * Adds all available stylesheet sets to the View > Use Style submenu
 *
 * If all frames have preferred stylesheet sets then the "Default style"
 * menuitem should remain hidden, otherwise it should be shown, and
 * if some frames have a selected stylesheet then the "Default style"
 * menuitem should be unchecked, otherwise it should remain checked.
 *
 * A stylesheet set's menuitem should not be checked if the "None" style
 * is currently selected. Otherwise a stylesheet set may be available in
 * more than one frame. In such a case the menuitem should only be checked
 * if it is selected in all frames in which it is available.
 *
 * @param menuPopup          The submenu's popup child
 * @param frameset           The frameset whose sets are to be added
 * @param styleDisabled      True if the "None" style is currently selected
 * @param itemPersistentOnly The "Default style" menuitem element
 */
function stylesheetFillAll(menuPopup, frameset, styleDisabled, itemPersistentOnly)
{
  stylesheetFillFrame(menuPopup, frameset, styleDisabled, itemPersistentOnly);
  for (var i = 0; i < frameset.frames.length; i++) {
    stylesheetFillAll(menuPopup, frameset.frames[i], styleDisabled, itemPersistentOnly);
  }
}
/**
 * Populates the View > Use Style submenu with all available stylesheet sets
 * @param menuPopup The submenu's popup child
 */
function stylesheetFillPopup(menuPopup)
{
  /* Clear menu */
  var itemPersistentOnly = menuPopup.firstChild.nextSibling;
  while (itemPersistentOnly.nextSibling)
    menuPopup.removeChild(itemPersistentOnly.nextSibling);

  /* Reset permanent items */
  var styleDisabled = getMarkupDocumentViewer().authorStyleDisabled;
  menuPopup.firstChild.setAttribute("checked", styleDisabled);
  itemPersistentOnly.setAttribute("checked", !styleDisabled);
  itemPersistentOnly.hidden = true;

  stylesheetFillAll(menuPopup, window.content, styleDisabled, itemPersistentOnly);
}
/**
 * Switches all frames in a frameset to the same stylesheet set
 *
 * Only frames that support the given title will be switched
 *
 * @param frameset The frameset whose frames are to be switched
 * @param title    The name of the stylesheet set to switch to
 */
function stylesheetSwitchAll(frameset, title) {
  if (!title || frameset.document.styleSheetSets.contains(title)) {
    frameset.document.selectedStyleSheetSet = title;
  }
  for (var i = 0; i < frameset.frames.length; i++) {
    stylesheetSwitchAll(frameset.frames[i], title);
  }
}

function setStyleDisabled(disabled) {
  getMarkupDocumentViewer().authorStyleDisabled = disabled;
}

function restartApp() {
  // Notify all windows that an application quit has been requested.
  var cancelQuit = Components.classes["@mozilla.org/supports-PRBool;1"]
                             .createInstance(Components.interfaces.nsISupportsPRBool);

  Services.obs.notifyObservers(cancelQuit, "quit-application-requested", "restart");

  // Something aborted the quit process.
  if (cancelQuit.data)
    return;

  Services.prefs.setBoolPref("browser.sessionstore.resume_session_once", true);
  const nsIAppStartup = Components.interfaces.nsIAppStartup;
  Components.classes["@mozilla.org/toolkit/app-startup;1"]
            .getService(nsIAppStartup)
            .quit(nsIAppStartup.eRestart | nsIAppStartup.eAttemptQuit);
 }

function applyTheme(themeName)
{
  var name = themeName.getAttribute("internalName");
  if (!name)
    return;

  var str = Components.classes["@mozilla.org/supports-string;1"]
                      .createInstance(Components.interfaces.nsISupportsString);
  str.data = name;

  if (Services.prefs.getBoolPref("extensions.dss.enabled")) {
    Services.prefs.setComplexValue("general.skins.selectedSkin",
                                   Components.interfaces.nsISupportsString, str);
    return;
  }

  Services.prefs.setComplexValue("extensions.lastSelectedSkin",
                                 Components.interfaces.nsISupportsString, str);
  var switchPending =
      str != Services.prefs.getComplexValue("general.skins.selectedSkin",
                                            Components.interfaces.nsISupportsString);
  Services.prefs.setBoolPref("extensions.dss.switchPending", switchPending);

  if (switchPending) {
    var promptTitle = gNavigatorBundle.getString("switchskinstitle");
    var brandName = gBrandBundle.getString("brandShortName");
    var promptMsg = gNavigatorBundle.getFormattedString("switchskins", [brandName]);
    var promptNow = gNavigatorBundle.getString("switchskinsnow");
    var promptLater = gNavigatorBundle.getString("switchskinslater");
    var check = {value: false};
    var flags = Services.prompt.BUTTON_POS_0 * Services.prompt.BUTTON_TITLE_IS_STRING +
                Services.prompt.BUTTON_POS_1 * Services.prompt.BUTTON_TITLE_IS_STRING;
    var pressedVal = Services.prompt.confirmEx(window, promptTitle, promptMsg,
                                               flags, promptNow, promptLater,
                                               null, null, check);

    if (pressedVal == 0)
      restartApp();
  }
}

function getNewThemes()
{
  // get URL for more themes from prefs
  try {
    var formatter = Components.classes["@mozilla.org/toolkit/URLFormatterService;1"]
                              .getService(Components.interfaces.nsIURLFormatter);
    openTopWin(formatter.formatURLPref("extensions.getMoreThemesURL"));
  }
  catch (ex) {
    dump(ex);
  }
}

function URLBarFocusHandler(aEvent)
{
  if (gIgnoreFocus)
    gIgnoreFocus = false;
  else if (gClickSelectsAll)
    gURLBar.select();
}

function URLBarMouseDownHandler(aEvent)
{
  if (gURLBar.hasAttribute("focused")) {
    gIgnoreClick = true;
  } else {
    gIgnoreFocus = true;
    gIgnoreClick = false;
    gURLBar.setSelectionRange(0, 0);
  }
}

function URLBarClickHandler(aEvent)
{
  if (!gIgnoreClick && gClickSelectsAll && gURLBar.selectionStart == gURLBar.selectionEnd)
    if (gClickAtEndSelects || gURLBar.selectionStart < gURLBar.value.length)
      gURLBar.select();
}

// This function gets the shell service and has it check its setting
// This will do nothing on platforms without a shell service.
function checkForDefaultBrowser()
{
  const NS_SHELLSERVICE_CID = "@mozilla.org/suite/shell-service;1";
  
  if (NS_SHELLSERVICE_CID in Components.classes) {
    const nsIShellService = Components.interfaces.nsIShellService;
    var shellService = Components.classes["@mozilla.org/suite/shell-service;1"]
                                 .getService(nsIShellService);
    var appTypes = shellService.shouldBeDefaultClientFor;

    // show the default client dialog only if we should check for the default
    // client and we aren't already the default for the stored app types in
    // shell.checkDefaultApps
    if (appTypes && shellService.shouldCheckDefaultClient &&
        !shellService.isDefaultClient(true, appTypes)) {
      window.openDialog("chrome://communicator/content/defaultClientDialog.xul",
                        "DefaultClient",
                        "modal,centerscreen,chrome,resizable=no"); 
      // Force the sidebar to build since the windows 
      // integration dialog has come up.
      SidebarRebuild();
    }
  }
}

function ShowAndSelectContentsOfURLBar()
{
  if (!isElementVisible(gURLBar)) {
    BrowserOpenWindow();
    return;
  }

  if (!gURLBar.readOnly) {
    if (gURLBar.value)
      gURLBar.select();
    else
      gURLBar.focus();
  }
}

// If "ESC" is pressed in the url bar, we replace the urlbar's value with the url of the page
// and highlight it, unless it is about:blank, where we reset it to "".
function handleURLBarRevert()
{
  var url = getWebNavigation().currentURI.spec;
  var throbberElement = document.getElementById("navigator-throbber");

  var isScrolling = gURLBar.userAction == "scrolling";
  
  // don't revert to last valid url unless page is NOT loading
  // and user is NOT key-scrolling through autocomplete list
  if (!throbberElement.hasAttribute("busy") && !isScrolling) {
    URLBarSetURI();

    // If the value isn't empty, select it.
    if (gURLBar.value)
      gURLBar.select();
  }

  // tell widget to revert to last typed text only if the user
  // was scrolling when they hit escape
  return isScrolling;
}

function handleURLBarCommand(aUserAction, aTriggeringEvent)
{
  try {
    addToUrlbarHistory(gURLBar.value);
  } catch (ex) {
    // Things may go wrong when adding url to session history,
    // but don't let that interfere with the loading of the url.
  }
  
  BrowserLoadURL(aTriggeringEvent); 
}

function UpdatePageProxyState()
{
  if (gURLBar.value != gLastValidURLStr)
    SetPageProxyState("invalid", null);
}

function SetPageProxyState(aState, aURI)
{
  if (!gProxyButton)
    gProxyButton = document.getElementById("page-proxy-button");
  if (!gProxyFavIcon)
    gProxyFavIcon = document.getElementById("page-proxy-favicon");
  if (!gProxyDeck)
    gProxyDeck = document.getElementById("page-proxy-deck");

  gProxyButton.setAttribute("pageproxystate", aState);

  if (aState == "valid") {
    gLastValidURLStr = gURLBar.value;
    gURLBar.addEventListener("input", UpdatePageProxyState, false);
    if (gBrowser.shouldLoadFavIcon(aURI)) {
      var favStr = gBrowser.buildFavIconString(aURI);
      if (favStr != gProxyFavIcon.src) {
        gBrowser.loadFavIcon(aURI, "src", gProxyFavIcon);
        gProxyDeck.selectedIndex = 0;
      }
      else gProxyDeck.selectedIndex = 1;
    }
    else {
      gProxyDeck.selectedIndex = 0;
      gProxyFavIcon.removeAttribute("src");
    }
  } else if (aState == "invalid") {
    gURLBar.removeEventListener("input", UpdatePageProxyState, false);
    gProxyDeck.selectedIndex = 0;
  }
}

function PageProxyDragGesture(aEvent)
{
  if (gProxyButton.getAttribute("pageproxystate") == "valid") {
    nsDragAndDrop.startDrag(aEvent, proxyIconDNDObserver);
    return true;
  }
  return false;
}

function handlePageProxyClick(aEvent)
{
  switch (aEvent.button) {
  case 0:
    // bug 52784 - select location field contents
    gURLBar.select();
    break;
  case 1:
    // bug 111337 - load url/keyword from clipboard
    return middleMousePaste(aEvent);
    break;
  }
  return true;
}

function updateComponentBarBroadcaster()
{ 
  var compBarBroadcaster = document.getElementById('cmd_viewcomponentbar');
  var taskBarBroadcaster = document.getElementById('cmd_viewtaskbar');
  var compBar = document.getElementById('component-bar');
  if (taskBarBroadcaster.getAttribute('checked') == 'true') {
    compBarBroadcaster.removeAttribute('disabled');
    if (compBar.getAttribute('hidden') != 'true')
      compBarBroadcaster.setAttribute('checked', 'true');
  }
  else {
    compBarBroadcaster.setAttribute('disabled', 'true');
    compBarBroadcaster.removeAttribute('checked');
  }
}

function updateToolbarStates(toolbarMenuElt)
{
  updateComponentBarBroadcaster();

  const tabbarMenuItem = document.getElementById("menuitem_showhide_tabbar");
  // Make show/hide menu item reflect current state
  const visibility = gBrowser.getStripVisibility();
  tabbarMenuItem.setAttribute("checked", visibility);

  // Don't allow the tab bar to be shown/hidden when more than one tab is open
  // or when we have 1 tab and the autoHide pref is set
  const disabled = gBrowser.browsers.length > 1 ||
                   Services.prefs.getBoolPref("browser.tabs.autoHide");
  tabbarMenuItem.setAttribute("disabled", disabled);
}

function showHideTabbar()
{
  const visibility = gBrowser.getStripVisibility();
  Services.prefs.setBoolPref("browser.tabs.forceHide", visibility);
  gBrowser.setStripVisibilityTo(!visibility);
}

function BrowserFullScreen()
{
  window.fullScreen = !window.fullScreen;
}

function onFullScreen()
{
  FullScreen.toggle();
}

function UpdateStatusBarPopupIcon(aEvent)
{
  if (aEvent && aEvent.originalTarget != gBrowser.getNotificationBox())
    return;

  var showIcon = Services.prefs.getBoolPref("privacy.popups.statusbar_icon_enabled");
  if (showIcon) {
    var popupIcon = document.getElementById("popupIcon");
    popupIcon.hidden = !gBrowser.getNotificationBox().popupCount;
  }
}

function StatusbarViewPopupManager()
{
  var hostPort = "";
  try {
    hostPort = getBrowser().selectedBrowser.currentURI.hostPort;
  }
  catch(ex) { }
  
  // open whitelist with site prefilled to unblock
  viewPopups(hostPort);
}

function popupBlockerMenuShowing(event)
{
  var separator = document.getElementById("popupMenuSeparator");

  if (separator)
    separator.hidden = !createShowPopupsMenu(event.target, gBrowser.selectedBrowser);
}

function toHistory()
{
  toOpenWindowByType("history:manager", "chrome://communicator/content/history/history.xul");
}

function checkTheme(popup)
{
  var prefName = Services.prefs.getBoolPref("extensions.dss.switchPending") ?
                 "extensions.lastSelectedSkin" : "general.skins.selectedSkin";
  var currentTheme = Services.prefs.getComplexValue(prefName,
                         Components.interfaces.nsISupportsString);
  var menuitem = popup.getElementsByAttribute("internalName", currentTheme)[0];
  if (menuitem)
    menuitem.setAttribute("checked", true);
}

// opener may not have been initialized by load time (chrome windows only)
// so call this function some time later.
function maybeInitPopupContext()
{
  // it's not a popup with no opener
  if (!window.content.opener)
    return null;

  try {
    // are we a popup window?
    const CI = Components.interfaces;
    var xulwin = window
                 .QueryInterface(CI.nsIInterfaceRequestor)
                 .getInterface(CI.nsIWebNavigation)
                 .QueryInterface(CI.nsIDocShellTreeItem).treeOwner
                 .QueryInterface(CI.nsIInterfaceRequestor)
                 .getInterface(CI.nsIXULWindow);
    if (xulwin.contextFlags &
        CI.nsIWindowCreator2.PARENT_IS_LOADING_OR_RUNNING_TIMEOUT) {
      // return our opener's URI
      return Services.io.newURI(window.content.opener.location.href, null, null);
    }
  } catch(e) {
  }
  return null;
}

function WindowIsClosing()
{
  var browser = getBrowser();
  var cn = browser.tabContainer.childNodes;
  var numtabs = cn.length;
  var reallyClose = true;

  if (!/Mac/.test(navigator.platform) && isClosingLastBrowser()) {
    let closingCanceled = Components.classes["@mozilla.org/supports-PRBool;1"]
                                    .createInstance(Components.interfaces.nsISupportsPRBool);
    Services.obs.notifyObservers(closingCanceled, "browser-lastwindow-close-requested", null);
    if (closingCanceled.data)
      return false;

    Services.obs.notifyObservers(null, "browser-lastwindow-close-granted", null);

    return true;
  }

  if (numtabs > 1) {
    var shouldPrompt = Services.prefs.getBoolPref("browser.tabs.warnOnClose");
    if (shouldPrompt) {
      //default to true: if it were false, we wouldn't get this far
      var warnOnClose = {value:true};

       var buttonPressed = promptService.confirmEx(window, 
         gNavigatorBundle.getString('tabs.closeWarningTitle'), 
         gNavigatorBundle.getFormattedString("tabs.closeWarning", [numtabs]),
         (Services.prompt.BUTTON_TITLE_IS_STRING * Services.prompt.BUTTON_POS_0)
          + (Services.prompt.BUTTON_TITLE_CANCEL * Services.prompt.BUTTON_POS_1),
            gNavigatorBundle.getString('tabs.closeButton'),
            null, null,
            gNavigatorBundle.getString('tabs.closeWarningPromptMe'),
            warnOnClose);
      reallyClose = (buttonPressed == 0);
      //don't set the pref unless they press OK and it's false
      if (reallyClose && !warnOnClose.value) {
        Services.prefs.setBoolPref("browser.tabs.warnOnClose", false);
      }
    } //if the warn-me pref was true
  } //if multiple tabs are open

  for (var i = 0; reallyClose && i < numtabs; ++i) {
    var ds = browser.getBrowserForTab(cn[i]).docShell;
  
    if (ds.contentViewer && !ds.contentViewer.permitUnload())
      reallyClose = false;
  }

  return reallyClose;
}

/**
 * Checks whether this is the last full *browser* window around.
 * @returns true if closing last browser window, false if not.
 */
function isClosingLastBrowser() {
  // Popups aren't considered full browser windows.
  if (!toolbar.visible)
    return false;

  // Figure out if there's at least one other browser window around.
  var e = Services.wm.getEnumerator("navigator:browser");
  while (e.hasMoreElements()) {
    let win = e.getNext();
    if (win != window && win.toolbar.visible)
      return false;
  }

  return true;
}

/**
 * file upload support
 */

/* This function returns the URI of the currently focused content frame
 * or frameset.
 */
function getCurrentURI()
{
  const CI = Components.interfaces;

  var focusedWindow = document.commandDispatcher.focusedWindow;
  var contentFrame = isContentFrame(focusedWindow) ? focusedWindow : window.content;

  var nav = contentFrame.QueryInterface(CI.nsIInterfaceRequestor)
                        .getInterface(CI.nsIWebNavigation);
  return nav.currentURI;
}

function uploadFile(fileURL)
{
  const CI = Components.interfaces;

  var targetBaseURI = getCurrentURI();

  // generate the target URI.  we use fileURL.file.leafName to get the
  // unicode value of the target filename w/o any URI-escaped chars.
  // this gives the protocol handler the best chance of generating a
  // properly formatted URI spec.  we pass null for the origin charset
  // parameter since we want the URI to inherit the origin charset
  // property from targetBaseURI.

  var leafName = fileURL.QueryInterface(CI.nsIFileURL).file.leafName;

  var targetURI = Services.io.newURI(leafName, null, targetBaseURI);

  // ok, start uploading...
  openDialog("chrome://communicator/content/downloads/uploadProgress.xul", "",
             "titlebar,centerscreen,minimizable,dialog=no", fileURL, targetURI);
}

function BrowserUploadFile()
{
  try {
    uploadFile(selectFileToOpen("uploadFile", "browser.upload."));
  } catch (e) {}
}

/* This function is called whenever the file menu is about to be displayed.
 * Enable the upload menu item if appropriate. */
function updateFileUploadItem()
{
  var canUpload = false;
  try {
    canUpload = getCurrentURI().schemeIs('ftp');
  } catch (e) {}

  var item = document.getElementById('Browser:UploadFile');
  if (canUpload)
    item.removeAttribute('disabled');
  else
    item.setAttribute('disabled', 'true');
}

function isBidiEnabled()
{
  var rv = false;

  var systemLocale;
  try {
    systemLocale = Services.locale.getSystemLocale()
                                  .getCategory("NSILOCALE_CTYPE");
    rv = /^(he|ar|syr|fa|ur)-/.test(systemLocale);
  } catch (e) {}

  if (!rv) {
    // check the overriding pref
    rv = Services.prefs.getBoolPref("bidi.browser.ui");
  }

  return rv;
}

function SwitchDocumentDirection(aWindow)
{
  aWindow.document.dir = (aWindow.document.dir == "ltr" ? "rtl" : "ltr");

  for (var run = 0; run < aWindow.frames.length; run++)
    SwitchDocumentDirection(aWindow.frames[run]);
}

function updateSavePageItems()
{
  var autoDownload = Services.prefs
                             .getBoolPref("browser.download.useDownloadDir");
  goSetMenuValue("savepage", autoDownload ? "valueSave" : "valueSaveAs");
}

function convertFromUnicode(charset, str)
{
  try {
    var unicodeConverter = Components
       .classes["@mozilla.org/intl/scriptableunicodeconverter"]
       .createInstance(Components.interfaces.nsIScriptableUnicodeConverter);
    unicodeConverter.charset = charset;
    str = unicodeConverter.ConvertFromUnicode(str);
    return str + unicodeConverter.Finish();
  } catch(ex) {
    return null; 
  }
}

function getNotificationBox(aWindow)
{
  return aWindow.QueryInterface(Components.interfaces.nsIInterfaceRequestor)
                .getInterface(Components.interfaces.nsIWebNavigation)
                .QueryInterface(Components.interfaces.nsIDocShell)
                .chromeEventHandler.parentNode.wrappedJSObject;
}

function BrowserToolboxCustomizeInit()
{
  toolboxCustomizeInit("main-menubar");
}

function BrowserToolboxCustomizeDone(aToolboxChanged)
{
  toolboxCustomizeDone("main-menubar", getNavToolbox(), aToolboxChanged);

  // Update the urlbar
  var value = gBrowser.userTypedValue;
  if (value == null) {
    let URL = getWebNavigation().currentURI.spec;
    if (URL != "about:blank" || content.opener) {
      // XXXRatty: we might need URL = losslessDecodeURI(uri) from Firefox
      // if we we are affected by bug 410726, bug 452979, etc.
      // (Unfortunately I don't have access to 452979)
      value = URL;
    }
    else
      value = "";
  }
  gURLBar.value = value;

  gButtonPrefListener.updateButton("browser.toolbars.showbutton.go");
  gButtonPrefListener.updateButton("browser.toolbars.showbutton.search");

  // XXXRatty: Remember to update Feedhandler once we get one.

  // fix up the personal toolbar folder
  var bt = document.getElementById("bookmarks-ptf");
  if (isElementVisible(bt)) {
    // XXXRatty: do we still need to reinstate the BookmarksToolbarRDFObserver?
    let btchevron = document.getElementById("bookmarks-chevron");
    // no uniqueness is guaranteed, so we have to remove first
    try {
      bt.database.RemoveObserver(BookmarksToolbarRDFObserver);
    } catch (ex) {
      // ignore
    }
    bt.database.AddObserver(BookmarksToolbarRDFObserver);
    bt.builder.rebuild();
    btchevron.builder.rebuild();

    // fake a resize; this function takes care of flowing bookmarks
    // from the bar to the overflow item
    BookmarksToolbar.resizeFunc(null);
  }
}

function BrowserToolboxCustomizeChange(event)
{
  toolboxCustomizeChange(getNavToolbox(), event);
}
