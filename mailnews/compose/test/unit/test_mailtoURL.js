/*
 * Test suite for mailto: URLs
 */

const nsIMailtoUrl = Components.interfaces.nsIMailtoUrl;
const COMPOSE_HTML = Components.interfaces.nsIMsgCompFormat.HTML;
const COMPOSE_DEFAULT = Components.interfaces.nsIMsgCompFormat.Default;

function run_test() {

  function test(aTest) {
    var ios = Components.classes["@mozilla.org/network/io-service;1"]
                        .getService(Components.interfaces.nsIIOService);
    var uri = ios.newURI(aTest.url, null, null);
    uri = uri.QueryInterface(Components.interfaces.nsIMailtoUrl);

    var to = {}, cc = {}, bcc = {}, subject = {}, body = {}, html = {},
        reference = {}, newsgroup = {}, composeformat = {};
    uri.GetMessageContents(to, cc, bcc, subject, body, html, reference,
                           newsgroup, composeformat);
    do_check_eq(aTest.to, to.value);
    do_check_eq(aTest.cc, cc.value);
    do_check_eq(aTest.bcc, bcc.value);
    do_check_eq(aTest.subject, subject.value);
    do_check_eq(aTest.body, body.value);
    do_check_eq(aTest.html, html.value);
    do_check_eq(aTest.reference, reference.value);
    do_check_eq(aTest.newsgroup, newsgroup.value);
    do_check_eq(aTest.composeformat, composeformat.value);
    do_check_eq(aTest.from, uri.fromPart);
    do_check_eq(aTest.followupto, uri.followUpToPart);
    do_check_eq(aTest.organization, uri.organizationPart);
    do_check_eq(aTest.replyto, uri.replyToPart);
    do_check_eq(aTest.priority, uri.priorityPart);
    do_check_eq(aTest.newshost, uri.newsHostPart);
    do_check_true(uri.equals(uri));
  }

  for (var i = 0; i < tests.length; i++)
    test(tests[i]);

};

const tests = [
  {
    url: "mailto:one@example.com",
    to: "one@example.com",
    cc: "",
    bcc: "",
    subject: "",
    body: "",
    html: "",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:two@example.com?",
    to: "two@example.com",
    cc: "",
    bcc: "",
    subject: "",
    body: "",
    html: "",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  /* the heirarchical-part address shouldn't be mime-decoded */
  {
    url: "mailto:%3D%3FUTF-8%3FQ%3Fthree%3F%3D@example.com",
    to: "=?UTF-8?Q?three?=@example.com",
    cc: "",
    bcc: "",
    subject: "",
    body: "",
    html: "",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  /* a to=address should be mime-decoded */
  {
    url: "mailto:?to=%3D%3FUTF-8%3FQ%3Ffour%3F%3D@example.com",
    to: "four@example.com",
    cc: "",
    bcc: "",
    subject: "",
    body: "",
    html: "",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:fivea@example.com?to=%3D%3FUTF-8%3FQ%3Ffiveb%3F%3D@example.com",
    to: "fivea@example.com, fiveb@example.com",
    cc: "",
    bcc: "",
    subject: "",
    body: "",
    html: "",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:sixa@example.com?to=sixb@example.com&to=sixc@example.com",
    to: "sixa@example.com, sixb@example.com, sixc@example.com",
    cc: "",
    bcc: "",
    subject: "",
    body: "",
    html: "",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:?cc=seven@example.com",
    to: "",
    cc: "seven@example.com",
    bcc: "",
    subject: "",
    body: "",
    html: "",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:?cc=%3D%3FUTF-8%3FQ%3Feight%3F%3D@example.com",
    to: "",
    cc: "eight@example.com",
    bcc: "",
    subject: "",
    body: "",
    html: "",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:?bcc=nine@example.com",
    to: "",
    cc: "",
    bcc: "nine@example.com",
    subject: "",
    body: "",
    html: "",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:?bcc=%3D%3FUTF-8%3FQ%3Ften%3F%3D@example.com",
    to: "",
    cc: "",
    bcc: "ten@example.com",
    subject: "",
    body: "",
    html: "",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:?subject=foo",
    to: "",
    cc: "",
    bcc: "",
    subject: "foo",
    body: "",
    html: "",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:?subject=%62%61%72",
    to: "",
    cc: "",
    bcc: "",
    subject: "bar",
    body: "",
    html: "",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:?subject=%3D%3Futf-8%3FQ%3F%3DC2%3DA1encoded_subject%21%3F%3D",
    to: "",
    cc: "",
    bcc: "",
    subject: "¡encoded subject!",
    body: "",
    html: "",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:?body=one%20body",
    to: "",
    cc: "",
    bcc: "",
    subject: "",
    body: "one body",
    html: "",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:?body=two%20bodies&body=two%20lines",
    to: "",
    cc: "",
    bcc: "",
    subject: "",
    body: "two bodies\ntwo lines",
    html: "",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:?html-part=html%20part",
    to: "",
    cc: "",
    bcc: "",
    subject: "",
    body: "",
    html: "html part",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_HTML,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:?html-body=html%20body",
    to: "",
    cc: "",
    bcc: "",
    subject: "",
    body: "",
    html: "html body",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_HTML,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:?html-part=html%20part&html-body=html-body%20trumps%20earlier%20html-part",
    to: "",
    cc: "",
    bcc: "",
    subject: "",
    body: "",
    html: "html-body trumps earlier html-part",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_HTML,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:?references=%3Cref1%40example.com%3E",
    to: "",
    cc: "",
    bcc: "",
    subject: "",
    body: "",
    html: "",
    reference: "<ref1@example.com>",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:?in-reply-to=%3Crepl1%40example.com%3E",
    to: "",
    cc: "",
    bcc: "",
    subject: "",
    body: "",
    html: "",
    reference: "<repl1@example.com>",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:?references=%3Cref2%40example.com%3E" +
         "&in-reply-to=%3Crepl2%40example.com%3E",
    to: "",
    cc: "",
    bcc: "",
    subject: "",
    body: "",
    html: "",
    reference: "<ref2@example.com> <repl2@example.com>",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:?references=%3Cref3%40example.com%3E%20%3Crepl3%40example.com%3E" +
         "&in-reply-to=%3Crepl3%40example.com%3E",
    to: "",
    cc: "",
    bcc: "",
    subject: "",
    body: "",
    html: "",
    reference: "<ref3@example.com> <repl3@example.com>",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:?newsgroups=mozilla.dev.apps.thunderbird",
    to: "",
    cc: "",
    bcc: "",
    subject: "",
    body: "",
    html: "",
    reference: "",
    newsgroup: "mozilla.dev.apps.thunderbird",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:?newsgroups=%3D%3FUTF-8%3FQ%3Fmozilla.test.multimedia%3F%3D",
    to: "",
    cc: "",
    bcc: "",
    subject: "",
    body: "",
    html: "",
    reference: "",
    newsgroup: "mozilla.test.multimedia",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:?from=notlikely@example.com",
    to: "",
    cc: "",
    bcc: "",
    subject: "",
    body: "",
    html: "",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "notlikely@example.com",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:?from=%3D%3FUTF-8%3FQ%3Fme@example.com%3F%3D",
    to: "",
    cc: "",
    bcc: "",
    subject: "",
    body: "",
    html: "",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "me@example.com",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:?followup-to=mozilla.dev.planning",
    to: "",
    cc: "",
    bcc: "",
    subject: "",
    body: "",
    html: "",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "mozilla.dev.planning",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:?followup-to=%3D%3FUTF-8%3FQ%3Fmozilla.test%3F%3D",
    to: "",
    cc: "",
    bcc: "",
    subject: "",
    body: "",
    html: "",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "mozilla.test",
    organization: "",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:?organization=very%20little",
    to: "",
    cc: "",
    bcc: "",
    subject: "",
    body: "",
    html: "",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "very little",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:?organization=%3D%3FUTF-8%3FQ%3Fmicroscopic%3F%3D",
    to: "",
    cc: "",
    bcc: "",
    subject: "",
    body: "",
    html: "",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "microscopic",
    replyto: "",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:?reply-to=notme@example.com",
    to: "",
    cc: "",
    bcc: "",
    subject: "",
    body: "",
    html: "",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "",
    replyto: "notme@example.com",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:?reply-to=%3D%3FUTF-8%3FB%3Fw4VrZQ%3D%3D%3F%3D%20%3Cake@example.org%3E",
    to: "",
    cc: "",
    bcc: "",
    subject: "",
    body: "",
    html: "",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "",
    replyto: "Åke <ake@example.org>",
    priority: "",
    newshost: ""
  },
  {
    url: "mailto:?priority=1%20(People%20Are%20Dying!!1!)",
    to: "",
    cc: "",
    bcc: "",
    subject: "",
    body: "",
    html: "",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "1 (People Are Dying!!1!)",
    newshost: ""
  },
  {
    url: "mailto:?priority=%3D%3FUTF-8%3FQ%3F4%3F%3D",
    to: "",
    cc: "",
    bcc: "",
    subject: "",
    body: "",
    html: "",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "4",
    newshost: ""
  },
  {
    url: "mailto:?newshost=news.mozilla.org",
    to: "",
    cc: "",
    bcc: "",
    subject: "",
    body: "",
    html: "",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: "news.mozilla.org"
  },
  {
    url: "mailto:?newshost=%3D%3FUTF-8%3FQ%3Fnews.example.org%3F%3D",
    to: "",
    cc: "",
    bcc: "",
    subject: "",
    body: "",
    html: "",
    reference: "",
    newsgroup: "",
    composeformat: COMPOSE_DEFAULT,
    from: "",
    followupto: "",
    organization: "",
    replyto: "",
    priority: "",
    newshost: "news.example.org"
  }
];
