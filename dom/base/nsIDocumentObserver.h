/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef nsIDocumentObserver_h___
#define nsIDocumentObserver_h___

#include "nsISupports.h"
#include "nsIMutationObserver.h"
#include "mozilla/dom/RustTypes.h"

namespace mozilla {

namespace dom {
class Document;
class Element;
}  // namespace dom
}  // namespace mozilla

#define NS_IDOCUMENT_OBSERVER_IID \
  {0x71041fa3, 0x6dd7, 0x4cde, {0xbb, 0x76, 0xae, 0xcc, 0x69, 0xe1, 0x75, 0x78}}

// Document observer interface
class nsIDocumentObserver : public nsIMutationObserver {
 public:
  NS_INLINE_DECL_STATIC_IID(NS_IDOCUMENT_OBSERVER_IID)

  /**
   * Notify that a content model update is beginning. This call can be
   * nested.
   */
  virtual void BeginUpdate(mozilla::dom::Document*) = 0;

  /**
   * Notify that a content model update is finished. This call can be
   * nested.
   */
  virtual void EndUpdate(mozilla::dom::Document*) = 0;

  /**
   * Notify the observer that a document load is beginning.
   */
  virtual void BeginLoad(mozilla::dom::Document*) = 0;

  /**
   * Notify the observer that a document load has finished. Note that
   * the associated reflow of the document will be done <b>before</b>
   * EndLoad is invoked, not after.
   */
  virtual void EndLoad(mozilla::dom::Document*) = 0;

  /**
   * Notification that the state of an element has changed. (ie: gained or lost
   * focus, became active or hovered over)
   *
   * This method is called automatically by elements when their state is changed
   * (therefore there is normally no need to invoke this method directly).
   *
   * This notification is not sent when elements are added/removed from the
   * document (the other notifications are used for that).
   *
   * @param Document The document being observed
   * @param Element the piece of content that changed
   * @param ElementState the element states that changed
   */
  virtual void ElementStateChanged(mozilla::dom::Document*,
                                   mozilla::dom::Element*,
                                   mozilla::dom::ElementState) = 0;
};

#define NS_DECL_NSIDOCUMENTOBSERVER_BEGINUPDATE \
  virtual void BeginUpdate(mozilla::dom::Document*) override;

#define NS_DECL_NSIDOCUMENTOBSERVER_ENDUPDATE \
  virtual void EndUpdate(mozilla::dom::Document*) override;

#define NS_DECL_NSIDOCUMENTOBSERVER_BEGINLOAD \
  virtual void BeginLoad(mozilla::dom::Document*) override;

#define NS_DECL_NSIDOCUMENTOBSERVER_ENDLOAD \
  virtual void EndLoad(mozilla::dom::Document*) override;

#define NS_DECL_NSIDOCUMENTOBSERVER_CONTENTSTATECHANGED     \
  virtual void ElementStateChanged(mozilla::dom::Document*, \
                                   mozilla::dom::Element*,  \
                                   mozilla::dom::ElementState) override;

#define NS_DECL_NSIDOCUMENTOBSERVER               \
  NS_DECL_NSIDOCUMENTOBSERVER_BEGINUPDATE         \
  NS_DECL_NSIDOCUMENTOBSERVER_ENDUPDATE           \
  NS_DECL_NSIDOCUMENTOBSERVER_BEGINLOAD           \
  NS_DECL_NSIDOCUMENTOBSERVER_ENDLOAD             \
  NS_DECL_NSIDOCUMENTOBSERVER_CONTENTSTATECHANGED \
  NS_DECL_NSIMUTATIONOBSERVER

#define NS_IMPL_NSIDOCUMENTOBSERVER_CORE_STUB(_class)  \
  void _class::BeginUpdate(mozilla::dom::Document*) {} \
  void _class::EndUpdate(mozilla::dom::Document*) {}   \
  NS_IMPL_NSIMUTATIONOBSERVER_CORE_STUB(_class)

#define NS_IMPL_NSIDOCUMENTOBSERVER_LOAD_STUB(_class) \
  void _class::BeginLoad(mozilla::dom::Document*) {}  \
  void _class::EndLoad(mozilla::dom::Document*) {}

#define NS_IMPL_NSIDOCUMENTOBSERVER_STATE_STUB(_class)      \
  void _class::ElementStateChanged(mozilla::dom::Document*, \
                                   mozilla::dom::Element*,  \
                                   mozilla::dom::ElementState) {}

#define NS_IMPL_NSIDOCUMENTOBSERVER_CONTENT(_class) \
  NS_IMPL_NSIMUTATIONOBSERVER_CONTENT(_class)

#endif /* nsIDocumentObserver_h___ */
