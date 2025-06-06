/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// Types of animation types of longhand properties.
exports.ANIMATION_TYPE_FOR_LONGHANDS = [
  [
    "discrete",
    new Set([
      "anchor-name",
      "anchor-scope",
      "align-content",
      "align-items",
      "align-self",
      "aspect-ratio",
      "appearance",
      "backface-visibility",
      "background-attachment",
      "background-blend-mode",
      "background-clip",
      "background-image",
      "background-origin",
      "background-repeat",
      "baseline-source",
      "border-bottom-style",
      "border-collapse",
      "border-image-repeat",
      "border-image-source",
      "border-left-style",
      "border-right-style",
      "border-top-style",
      "-moz-box-align",
      "box-decoration-break",
      "-moz-box-direction",
      "-moz-box-ordinal-group",
      "-moz-box-orient",
      "-moz-box-pack",
      "box-sizing",
      "caption-side",
      "clear",
      "clip-rule",
      "color-interpolation",
      "color-interpolation-filters",
      "color-scheme",
      "column-fill",
      "column-rule-style",
      "column-span",
      "contain",
      "content",
      "counter-increment",
      "counter-reset",
      "counter-set",
      "cursor",
      "direction",
      "dominant-baseline",
      "empty-cells",
      "field-sizing",
      "fill-rule",
      "flex-direction",
      "flex-wrap",
      "float",
      "-moz-float-edge",
      "font-family",
      "font-feature-settings",
      "font-kerning",
      "font-language-override",
      "font-palette",
      "font-style",
      "font-synthesis-weight",
      "font-synthesis-style",
      "font-synthesis-small-caps",
      "font-synthesis-position",
      "font-variant-alternates",
      "font-variant-caps",
      "font-variant-east-asian",
      "font-variant-emoji",
      "font-variant-ligatures",
      "font-variant-numeric",
      "font-variant-position",
      "-moz-force-broken-image-icon",
      "forced-color-adjust",
      "grid-auto-columns",
      "grid-auto-flow",
      "grid-auto-rows",
      "grid-column-end",
      "grid-column-start",
      "grid-row-end",
      "grid-row-start",
      "grid-template-areas",
      "grid-template-columns",
      "grid-template-rows",
      "hyphenate-character",
      "hyphens",
      "image-orientation",
      "image-rendering",
      "ime-mode",
      "-moz-inert",
      "initial-letter",
      "isolation",
      "justify-content",
      "justify-items",
      "justify-self",
      "line-break",
      "list-style-image",
      "list-style-position",
      "list-style-type",
      "marker-end",
      "marker-mid",
      "marker-start",
      "mask-clip",
      "mask-composite",
      "mask-image",
      "mask-mode",
      "mask-origin",
      "mask-repeat",
      "mask-type",
      "masonry-auto-flow",
      "mix-blend-mode",
      "object-fit",
      "-moz-orient",
      "-moz-osx-font-smoothing",
      "-moz-subtree-hidden-only-visually",
      "outline-style",
      "overflow-anchor",
      "overflow-block",
      "overflow-clip-box-block",
      "overflow-clip-box-inline",
      "overflow-inline",
      "overflow-wrap",
      "overflow-x",
      "overflow-y",
      "overscroll-behavior-inline",
      "overscroll-behavior-block",
      "overscroll-behavior-x",
      "overscroll-behavior-y",
      "break-after",
      "break-before",
      "break-inside",
      "page",
      "paint-order",
      "pointer-events",
      "position",
      "position-anchor",
      "position-area",
      "position-try-fallbacks",
      "position-try-order",
      "position-visibility",
      "print-color-adjust",
      "quotes",
      "resize",
      "ruby-align",
      "ruby-position",
      "scroll-behavior",
      "scroll-snap-align",
      "scroll-snap-stop",
      "scroll-snap-type",
      "shape-rendering",
      "scrollbar-gutter",
      "scrollbar-width",
      "stroke-linecap",
      "stroke-linejoin",
      "table-layout",
      "text-align",
      "text-align-last",
      "text-anchor",
      "text-combine-upright",
      "text-decoration-line",
      "text-decoration-skip-ink",
      "text-decoration-style",
      "text-emphasis-position",
      "text-emphasis-style",
      "text-justify",
      "text-orientation",
      "text-overflow",
      "text-rendering",
      "-moz-text-size-adjust",
      "-webkit-text-security",
      "-webkit-text-stroke-width",
      "text-transform",
      "text-underline-position",
      "text-wrap-mode",
      "text-wrap-style",
      "touch-action",
      "transform-box",
      "transform-style",
      "unicode-bidi",
      "-moz-user-focus",
      "-moz-user-input",
      "user-select",
      "vector-effect",
      "view-transition-class",
      "view-transition-name",
      "visibility",
      "white-space-collapse",
      "will-change",
      "-moz-window-dragging",
      "word-break",
      "writing-mode",
    ]),
  ],
  [
    "none",
    new Set([
      "animation-composition",
      "animation-delay",
      "animation-direction",
      "animation-duration",
      "animation-fill-mode",
      "animation-iteration-count",
      "animation-name",
      "animation-play-state",
      "animation-timeline",
      "animation-timing-function",
      "block-size",
      "border-block-end-color",
      "border-block-end-style",
      "border-block-end-width",
      "border-block-start-color",
      "border-block-start-style",
      "border-block-start-width",
      "border-inline-end-color",
      "border-inline-end-style",
      "border-inline-end-width",
      "border-inline-start-color",
      "border-inline-start-style",
      "border-inline-start-width",
      "container-name",
      "container-type",
      "contain-intrinsic-block-size",
      "contain-intrinsic-inline-size",
      "contain-intrinsic-height",
      "contain-intrinsic-width",
      "content-visibility",
      "-moz-context-properties",
      "-moz-control-character-visibility",
      "-moz-default-appearance",
      "-moz-theme",
      "display",
      "font-optical-sizing",
      "inline-size",
      "inset-block-end",
      "inset-block-start",
      "inset-inline-end",
      "inset-inline-start",
      "margin-block-end",
      "margin-block-start",
      "margin-inline-end",
      "margin-inline-start",
      "math-style",
      "max-block-size",
      "max-inline-size",
      "min-block-size",
      "-moz-min-font-size-ratio",
      "min-inline-size",
      "padding-block-end",
      "padding-block-start",
      "padding-inline-end",
      "padding-inline-start",
      "page-orientation",
      "math-depth",
      "-moz-box-collapse",
      "-moz-top-layer",
      "scroll-timeline-axis",
      "scroll-timeline-name",
      "size",
      "transition-behavior",
      "transition-delay",
      "transition-duration",
      "transition-property",
      "transition-timing-function",
      "view-timeline-axis",
      "view-timeline-inset",
      "view-timeline-name",
      "-moz-window-shadow",
    ]),
  ],
  [
    "color",
    new Set([
      "background-color",
      "border-bottom-color",
      "border-left-color",
      "border-right-color",
      "border-top-color",
      "accent-color",
      "caret-color",
      "color",
      "column-rule-color",
      "flood-color",
      "lighting-color",
      "outline-color",
      "scrollbar-color",
      "stop-color",
      "text-decoration-color",
      "text-emphasis-color",
      "-webkit-text-fill-color",
      "-webkit-text-stroke-color",
    ]),
  ],
  [
    "custom",
    new Set([
      "backdrop-filter",
      "background-position-x",
      "background-position-y",
      "background-size",
      "border-bottom-width",
      "border-image-slice",
      "border-image-outset",
      "border-image-width",
      "border-left-width",
      "border-right-width",
      "border-spacing",
      "border-top-width",
      "clip",
      "clip-path",
      "column-count",
      "column-rule-width",
      "d",
      "filter",
      "font-stretch",
      "font-variation-settings",
      "font-weight",
      "hyphenate-limit-chars",
      "mask-position-x",
      "mask-position-y",
      "mask-size",
      "object-position",
      "offset-anchor",
      "offset-path",
      "offset-position",
      "offset-rotate",
      "order",
      "perspective-origin",
      "rotate",
      "scale",
      "shape-outside",
      "stroke-dasharray",
      "transform",
      "transform-origin",
      "translate",
      "-moz-window-transform",
      "-webkit-line-clamp",
    ]),
  ],
  [
    "coord",
    new Set([
      "border-bottom-left-radius",
      "border-bottom-right-radius",
      "border-top-left-radius",
      "border-top-right-radius",
      "border-start-start-radius",
      "border-start-end-radius",
      "border-end-start-radius",
      "border-end-end-radius",
      "bottom",
      "column-gap",
      "column-width",
      "cx",
      "cy",
      "flex-basis",
      "height",
      "left",
      "letter-spacing",
      "line-height",
      "margin-bottom",
      "margin-left",
      "margin-right",
      "margin-top",
      "max-height",
      "max-width",
      "min-height",
      "min-width",
      "offset-distance",
      "padding-bottom",
      "padding-left",
      "padding-right",
      "padding-top",
      "perspective",
      "r",
      "rx",
      "ry",
      "right",
      "row-gap",
      "scroll-padding-block-start",
      "scroll-padding-block-end",
      "scroll-padding-inline-start",
      "scroll-padding-inline-end",
      "scroll-padding-top",
      "scroll-padding-right",
      "scroll-padding-bottom",
      "scroll-padding-left",
      "scroll-margin-block-start",
      "scroll-margin-block-end",
      "scroll-margin-inline-start",
      "scroll-margin-inline-end",
      "scroll-margin-top",
      "scroll-margin-right",
      "scroll-margin-bottom",
      "scroll-margin-left",
      "shape-margin",
      "stroke-dashoffset",
      "stroke-width",
      "tab-size",
      "text-indent",
      "text-decoration-thickness",
      "text-underline-offset",
      "top",
      "vertical-align",
      "width",
      "word-spacing",
      "x",
      "y",
      "z-index",
    ]),
  ],
  [
    "float",
    new Set([
      "-moz-box-flex",
      "fill-opacity",
      "flex-grow",
      "flex-shrink",
      "flood-opacity",
      "font-size-adjust",
      "opacity",
      "shape-image-threshold",
      "stop-opacity",
      "stroke-miterlimit",
      "stroke-opacity",
      "zoom",
      "-moz-window-opacity",
    ]),
  ],
  ["shadow", new Set(["box-shadow", "text-shadow"])],
  ["paintServer", new Set(["fill", "stroke"])],
  [
    "length",
    new Set([
      "font-size",
      "outline-offset",
      "outline-width",
      "overflow-clip-margin",
      "-moz-window-input-region-margin",
    ]),
  ],
];
