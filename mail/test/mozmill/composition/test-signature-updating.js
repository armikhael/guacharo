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
 * Steffen Wilberg <steffen.wilberg@web.de>
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

/**
 * Tests that the signature updates properly when switching identities.
 */

// make SOLO_TEST=composition/test-signature-updating.js mozmill-one

// mail.identity.id1.htmlSigFormat = false
// mail.identity.id1.htmlSigText   = "Tinderbox is soo 90ies"

// mail.identity.id2.htmlSigFormat = true
// mail.identity.id2.htmlSigText   = "Tinderboxpushlog is the new <b>hotness!</b>"

const MODULE_NAME = "test-signature-updating";

const RELATIVE_ROOT = "../shared-modules";
const MODULE_REQUIRES = ["folder-display-helpers", "compose-helpers", "window-helpers"];
var jumlib = {};
Components.utils.import("resource://mozmill/modules/jum.js", jumlib);
var elib = {};
Components.utils.import("resource://mozmill/modules/elementslib.js", elib);

var composeHelper = null;
var cwc = null; // compose window controller
var prefBranch = Cc["@mozilla.org/preferences-service;1"]
                   .getService(Ci.nsIPrefService).getBranch(null);

var setupModule = function (module) {
  let fdh = collector.getModule("folder-display-helpers");
  fdh.installInto(module);
  composeHelper = collector.getModule("compose-helpers");
  composeHelper.installInto(module);
  let wh = collector.getModule("window-helpers");
  wh.installInto(module);
};

function setupComposeWin(toAddr, subj, body) {
  cwc.type(cwc.a("addressingWidget", {class: "addressingWidgetCell", crazyDeck: 1}), toAddr);
  cwc.type(cwc.eid("msgSubject"), subj)
  cwc.type(cwc.eid("content-frame"), body);
}

/**
 * Test that the plaintext compose window has a signature initially,
 * and has the correct signature after switching to another identity.
 */
function testPlaintextComposeWindowSwitchSignatures() {
  prefBranch.setBoolPref("mail.identity.id1.compose_html", false);
  cwc = composeHelper.open_compose_new_mail();

  setupComposeWin("", "Plaintext compose window", "Body, first line.");

  let contentFrame = cwc.e("content-frame");
  let node = contentFrame.contentDocument.body.lastChild;

  // In plaintext compose, the signature is followed by two <br> elements.
  assert_equals(node.localName, "br");
  node = node.previousSibling;
  assert_equals(node.localName, "br");
  node = node.previousSibling;
  assert_equals(node.nodeValue, "Tinderbox is soo 90ies");
  node = node.previousSibling.previousSibling; // a <br> element, then the next text node
  assert_equals(node.nodeValue, "-- ");

  // Now switch identities!
  let menuID = cwc.e("msgIdentity");
  menuID.value = "id2";
  menuID.click();

  node = contentFrame.contentDocument.body.lastChild;

  // In plaintext compose, the signature is followed by two <br> elements.
  assert_equals(node.localName, "br");
  node = node.previousSibling;
  assert_equals(node.localName, "br");
  node = node.previousSibling;
  assert_equals(node.nodeValue, "Tinderboxpushlog is the new *hotness!*");
  node = node.previousSibling.previousSibling; // a <br> element, then the next text node
  assert_equals(node.nodeValue, "-- ");

  // Now check that the original signature has been removed!
  let bodyFirstChild =  contentFrame.contentDocument.body.firstChild;
  while (node != bodyFirstChild) {
    node = node.previousSibling;
    jumlib.assertNotEquals(node.nodeValue, "Tinderbox is soo 90ies");
    jumlib.assertNotEquals(node.nodeValue, "-- ");
  }
  assert_equals(node.nodeValue, "Body, first line.");

  composeHelper.close_compose_window(cwc);
}

/**
 * Same test, but with an HTML compose window
 */
function testHTMLComposeWindowSwitchSignatures() {
  prefBranch.setBoolPref("mail.identity.id1.compose_html", true);
  cwc = composeHelper.open_compose_new_mail();

  setupComposeWin("", "HTML compose window", "Body, first line.");

  let contentFrame = cwc.e("content-frame");
  let node = contentFrame.contentDocument.body.lastChild;

  // In html compose, the signature is inside the last node, which has a
  // class="moz-signature".
  assert_equals(node.className, "moz-signature");
  node = node.firstChild; // text node containing the signature divider
  assert_equals(node.nodeValue, "-- ");
  node = node.nextSibling;
  assert_equals(node.localName, "br");
  node = node.nextSibling;
  assert_equals(node.nodeValue, "Tinderbox is soo 90ies");

  // Now switch identities!
  let menuID = cwc.e("msgIdentity");
  menuID.value = "id2";
  menuID.click();

  node = contentFrame.contentDocument.body.lastChild;

  // In html compose, the signature is inside the last node
  // with class="moz-signature".
  assert_equals(node.className, "moz-signature");
  node = node.firstChild; // text node containing the signature divider
  assert_equals(node.nodeValue, "-- ");
  node = node.nextSibling;
  assert_equals(node.localName, "br");
  node = node.nextSibling;
  assert_equals(node.nodeValue, "Tinderboxpushlog is the new ");
  node = node.nextSibling;
  assert_equals(node.localName, "b");
  node = node.firstChild;
  assert_equals(node.nodeValue, "hotness!");

  // Now check that the original signature has been removed,
  // and no blank lines got added!
  node = contentFrame.contentDocument.body.firstChild;
  assert_equals(node.nodeValue, "Body, first line.");
  node = node.nextSibling;
  assert_equals(node.localName, "br");
  node = node.nextSibling;
  // check that the signature is immediately after the message text.
  assert_equals(node.className, "moz-signature");
  // check that that the signature is the last node.
  assert_equals(node, contentFrame.contentDocument.body.lastChild);

  composeHelper.close_compose_window(cwc);
}
