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
 * The Original Code is Mozilla Mozmill Test Code.
 *
 * The Initial Developer of the Original Code is Merike Sell.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Merike Sell <merikes@gmail.com>
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

var RELATIVE_ROOT = './shared-modules';
var MODULE_REQUIRES = ['CalendarUtils'];

var sleep = 500;
var calendar = "Mozmill";
var title1 = "Multiweek View Event";
var title2 = "Multiweek View Event Changed";
var desc = "Multiweek View Event Description";

var setupModule = function(module) {
  controller = mozmill.getMail3PaneController();
  CalendarUtils.createCalendar(calendar);
}

var testMultiWeekView = function () {
  let dateService = Components.classes["@mozilla.org/intl/scriptabledateformat;1"]
                              .getService(Components.interfaces.nsIScriptableDateFormat);
  // paths
  let miniMonth = '/id("messengerWindow")/id("tabmail-container")/id("tabmail")/'
    + 'id("tabpanelcontainer")/id("calendarTabPanel")/id("calendarContent")/id("ltnSidebar")/'
    + 'id("minimonth-pane")/{"align":"center"}/id("calMinimonthBox")/id("calMinimonth")/';
  let multiWeekView = '/id("messengerWindow")/id("tabmail-container")/id("tabmail")/'
    + 'id("tabpanelcontainer")/id("calendarTabPanel")/id("calendarContent")/'
    + 'id("calendarDisplayDeck")/id("calendar-view-box")/id("view-deck")/id("multiweek-view")/';
  let eventDialog = '/id("calendar-event-dialog")/id("event-grid")/id("event-grid-rows")/';
  let eventBox = multiWeekView + 'anon({"anonid":"mainbox"})/anon({"anonid":"monthgrid"})/'
    + 'anon({"anonid":"monthgridrows"})/[0]/{"selected":"true"}/'
    + '{"tooltip":"itemTooltip","calendar":"' + calendar.toLowerCase() + '"}/anon({"flex":"1"})/'
    + '[0]/anon({"anonid":"event-container"})/{"class":"calendar-event-selection"}/'
    + 'anon({"anonid":"eventbox"})/{"class":"calendar-event-details"}';
  
  controller.click(new elementslib.ID(controller.window.document, "calendar-tab-button"));
  controller.waitThenClick(new elementslib.ID(controller.window.document, "calendar-multiweek-view-button"));
  
  // pick year
  controller.click(new elementslib.Lookup(controller.window.document, miniMonth
    + 'anon({"anonid":"minimonth-header"})/anon({"anonid":"yearcell"})'));
  controller.click(new elementslib.Lookup(controller.window.document, miniMonth
    + 'anon({"anonid":"minimonth-header"})/anon({"anonid":"minmonth-popupset"})/'
    + 'anon({"anonid":"years-popup"})/[0]/{"value":"2009"}'));
  controller.sleep(sleep);
  
  // pick month
  controller.click(new elementslib.Lookup(controller.window.document, miniMonth
    + 'anon({"anonid":"minimonth-header"})/anon({"anonid":"monthheader"})'));
  controller.click(new elementslib.Lookup(controller.window.document, miniMonth
    + 'anon({"anonid":"minimonth-header"})/anon({"anonid":"minmonth-popupset"})/'
    + 'anon({"anonid":"months-popup"})/[0]/{"index":"0"}'));
  controller.sleep(sleep);

  // pick day
  controller.click(new elementslib.Lookup(controller.window.document, miniMonth
    + 'anon({"anonid":"minimonth-calendar"})/[1]/{"value":"1"}'));
  controller.sleep(sleep);
  
  // verify date
  let day = new elementslib.Lookup(controller.window.document, multiWeekView
    + 'anon({"anonid":"mainbox"})/anon({"anonid":"monthgrid"})/anon({"anonid":"monthgridrows"})/'
    + '[0]/{"selected":"true"}');
  controller.assertJS(day.getNode().mDate.icalString == "20090101");

  // create event
  // Thursday of 2009-01-01 should be the selected box in the first row with default settings
  let hour = new Date().getHours(); // remember time at click
  controller.doubleClick(new elementslib.Lookup(controller.window.document, multiWeekView
    + 'anon({"anonid":"mainbox"})/anon({"anonid":"monthgrid"})/anon({"anonid":"monthgridrows"})/'
    + '[0]/{"selected":"true"}/anon({"anonid":"day-items"})'));
  controller.waitForEval('utils.getWindows("Calendar:EventDialog").length > 0', sleep);
  let event = new mozmill.controller.MozMillController(mozmill.utils.getWindows("Calendar:EventDialog")[0]);
  event.sleep(sleep);
  
  // check that the start time is correct
  // next full hour except last hour hour of the day
  let nextHour = (hour == 23)? hour : (hour + 1) % 24;
  let startTime = nextHour + ':00';
  event.assertValue(new elementslib.Lookup(event.window.document, eventDialog
    + 'id("event-grid-startdate-row")/id("event-grid-startdate-picker-box")/'
    + 'id("event-starttime")/anon({"anonid":"hbox"})/anon({"anonid":"time-picker"})/'
    + 'anon({"class":"timepicker-box-class"})/anon({"class":"timepicker-text-class"})/'
    + 'anon({"flex":"1"})/anon({"anonid":"input"})'),
    startTime); 
  let date = dateService.FormatDate("", dateService.dateFormatShort,
    2009, 1, 1);
  event.assertValue(new elementslib.Lookup(event.window.document, eventDialog
    + 'id("event-grid-startdate-row")/id("event-grid-startdate-picker-box")/'
    + 'id("event-starttime")/anon({"anonid":"hbox"})/anon({"anonid":"date-picker"})/'
    + 'anon({"flex":"1","id":"hbox","class":"datepicker-box-class"})/'
    + '{"class":"datepicker-text-class"}/anon({"class":"menulist-editable-box textbox-input-box"})/'
    + 'anon({"anonid":"input"})'),
    date);
    
  // fill in title, description and calendar
  event.type(new elementslib.Lookup(event.window.document, eventDialog
    + 'id("event-grid-title-row")/id("item-title")/anon({"class":"textbox-input-box"})/'
    + 'anon({"anonid":"input"})'),
    title1);
  event.type(new elementslib.Lookup(event.window.document, eventDialog
    + 'id("event-grid-description-row")/id("item-description")/'
    + 'anon({"class":"textbox-input-box"})/anon({"anonid":"input"})'),
    desc);
  event.click(new elementslib.ID(event.window.document, "item-calendar"));
  event.click(new elementslib.Lookup(event.window.document, eventDialog
    + 'id("event-grid-category-color-row")/id("event-grid-category-box")/id("item-calendar")/'
    + '[0]/{"label":"' + calendar + '"}'));
  
  // save
  event.click(new elementslib.ID(event.window.document, "button-save"));
  controller.sleep(sleep);
  
  // if it was created successfully, it can be opened
  controller.doubleClick(new elementslib.Lookup(controller.window.document, eventBox));
  controller.waitForEval('utils.getWindows("Calendar:EventDialog").length > 0', sleep);
  event = new mozmill.controller.MozMillController(mozmill.utils.getWindows("Calendar:EventDialog")[0]);
  event.sleep(sleep);
  
  // change title and save changes
  event.type(new elementslib.Lookup(event.window.document, eventDialog
    + 'id("event-grid-title-row")/id("item-title")/anon({"class":"textbox-input-box"})/'
    + 'anon({"anonid":"input"})'),
    title2);
  event.click(new elementslib.ID(event.window.document, "button-save"));
  controller.sleep(sleep);
  
  // check if name was saved
  controller.assertValue(new elementslib.Lookup(controller.window.document, eventBox
    + '/{"flex":"1"}/anon({"anonid":"event-name"})'),
    title2);
  
  // delete event
  controller.click(new elementslib.Lookup(controller.window.document, eventBox));
  controller.keypress(new elementslib.ID(controller.window.document, "multiweek-view"),
    "VK_DELETE", {});
  controller.sleep(sleep);
  controller.assertNodeNotExist(new elementslib.Lookup(controller.window.document, eventBox));
}

var teardownTest = function(module) {
  CalendarUtils.deleteCalendars(calendar);
}
