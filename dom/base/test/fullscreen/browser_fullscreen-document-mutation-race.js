/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

requestLongerTimeout(2);

// Import helpers
Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/dom/base/test/fullscreen/fullscreen_helpers.js",
  this
);

add_setup(async function () {
  await pushPrefs(
    ["test.wait300msAfterTabSwitch", true],
    ["full-screen-api.transition-duration.enter", "0 0"],
    ["full-screen-api.transition-duration.leave", "0 0"],
    ["full-screen-api.allow-trusted-requests-only", false]
  );
});

async function startTests(setupFun, name) {
  TEST_URLS.forEach(url => {
    add_task(async () => {
      info(`Test ${name}, url: ${url}`);
      await BrowserTestUtils.withNewTab(
        {
          gBrowser,
          url,
        },
        async function (browser) {
          let promiseFsState = Promise.all([
            setupFun(browser),
            waitForFullscreenState(document, false, true),
          ]);
          // Trigger click event in inner most iframe
          SpecialPowers.spawn(
            browser.browsingContext.children[0].children[0],
            [],
            function () {
              content.setTimeout(() => {
                content.document.getElementById("div").click();
              }, 0);
            }
          );
          await promiseFsState;

          // Ensure the browser exits fullscreen state.
          ok(
            !window.fullScreen,
            "The chrome window should not be in fullscreen"
          );
          ok(
            !document.documentElement.hasAttribute("inDOMFullscreen"),
            "The chrome document should not be in fullscreen"
          );
        }
      );
    });
  });
}

function RemoveElementFromRemoteDocument(aBrowsingContext, aElementId) {
  return SpecialPowers.spawn(
    aBrowsingContext,
    [aElementId],
    async function (id) {
      content.document.addEventListener(
        "fullscreenchange",
        function () {
          content.document.getElementById(id).remove();
        },
        { once: true }
      );
    }
  );
}

startTests(async browser => {
  // toplevel
  let promise = waitRemoteFullscreenExitEvents([
    // browsingContext, name
    [browser.browsingContext, "toplevel"],
  ]);
  await RemoveElementFromRemoteDocument(browser.browsingContext, "div");
  return promise;
}, "document_mutation_toplevel");

startTests(async browser => {
  // middle iframe
  let promise = waitRemoteFullscreenExitEvents([
    // browsingContext, name
    [browser.browsingContext, "toplevel"],
    [browser.browsingContext.children[0], "middle"],
  ]);
  await RemoveElementFromRemoteDocument(
    browser.browsingContext.children[0],
    "div"
  );
  return promise;
}, "document_mutation_middle_frame");

startTests(async browser => {
  // innermost iframe
  let promise = waitRemoteFullscreenExitEvents([
    // browsingContext, name
    [browser.browsingContext, "toplevel"],
    [browser.browsingContext.children[0], "middle"],
    [browser.browsingContext.children[0].children[0], "inner"],
  ]);
  await RemoveElementFromRemoteDocument(
    browser.browsingContext.children[0].children[0],
    "div"
  );
  return promise;
}, "document_mutation_inner_frame");
