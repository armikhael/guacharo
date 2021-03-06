// Default start page
pref("mailnews.start_page.url","https://live.mozillamessaging.com/%APP%/start?locale=%LOCALE%&version=%VERSION%&os=%OS%&buildid=%APPBUILDID%");

// start page override to load after an update
pref("mailnews.start_page.override_url","https://live.mozillamessaging.com/%APP%/whatsnew?locale=%LOCALE%&version=%VERSION%esr&os=%OS%&buildid=%APPBUILDID%");

// Release notes URL
pref("app.releaseNotesURL", "http://live.mozillamessaging.com/%APP%/releasenotes?locale=%LOCALE%&version=%VERSION%esr&os=%OS%&buildid=%APPBUILDID%");

// Interval: Time between checks for a new version (in seconds)
// nightly=8 hours, official=24 hours
pref("app.update.interval", 86400);

// The time interval between the downloading of mar file chunks in the
// background (in seconds)
pref("app.update.download.backgroundInterval", 600);

// Give the user x seconds to react before showing the big UI. default=24 hours
pref("app.update.promptWaitTime", 86400);

pref("app.vendorURL", "http://www.mozilla.org/%LOCALE%/%APP%/");

pref("browser.search.param.ms-pc", "MOZT");
