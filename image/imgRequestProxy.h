/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_imgRequestProxy_h
#define mozilla_image_imgRequestProxy_h

#include "imgIRequest.h"

#include "nsIPrincipal.h"
#include "nsISupportsPriority.h"
#include "nsITimedChannel.h"
#include "nsCOMPtr.h"
#include "nsThreadUtils.h"
#include "mozilla/PreloaderBase.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/gfx/Rect.h"

#include "IProgressObserver.h"

#define NS_IMGREQUESTPROXY_CID                \
  {/* 20557898-1dd2-11b2-8f65-9c462ee2bc95 */ \
   0x20557898,                                \
   0x1dd2,                                    \
   0x11b2,                                    \
   {0x8f, 0x65, 0x9c, 0x46, 0x2e, 0xe2, 0xbc, 0x95}}

class imgCacheValidator;
class imgINotificationObserver;
class imgRequest;
class imgStatusNotifyRunnable;
class ProxyBehaviour;

namespace mozilla {
namespace image {
class Image;
class ProgressTracker;
}  // namespace image
}  // namespace mozilla

class imgRequestProxy : public mozilla::PreloaderBase,
                        public imgIRequest,
                        public mozilla::image::IProgressObserver,
                        public nsISupportsPriority,
                        public nsITimedChannel {
 protected:
  virtual ~imgRequestProxy();

 public:
  typedef mozilla::dom::Document Document;
  typedef mozilla::image::Image Image;
  typedef mozilla::image::ProgressTracker ProgressTracker;

  NS_INLINE_DECL_STATIC_IID(NS_IMGREQUESTPROXY_CID)
  MOZ_DECLARE_REFCOUNTED_TYPENAME(imgRequestProxy)
  NS_DECL_ISUPPORTS
  NS_DECL_IMGIREQUEST
  NS_DECL_NSIREQUEST
  NS_DECL_NSISUPPORTSPRIORITY
  // nsITimedChannel declared below

  imgRequestProxy();

  // Callers to Init or ChangeOwner are required to call NotifyListener after
  // (although not immediately after) doing so.
  nsresult Init(imgRequest* aOwner, nsILoadGroup* aLoadGroup, nsIURI* aURI,
                imgINotificationObserver* aObserver);

  nsresult ChangeOwner(imgRequest* aNewOwner);  // this will change mOwner.
                                                // Do not call this if the
                                                // previous owner has already
                                                // sent notifications out!

  // Add the request to the load group, if any. This should only be called once
  // during initialization.
  void AddToLoadGroup();

  inline bool HasObserver() const { return mListener != nullptr; }

  // Asynchronously notify this proxy's listener of the current state of the
  // image, and, if we have an imgRequest mOwner, any status changes that
  // happen between the time this function is called and the time the
  // notification is scheduled.
  void NotifyListener();

  // Synchronously notify this proxy's listener of the current state of the
  // image. Only use this function if you are currently servicing an
  // asynchronously-called function.
  void SyncNotifyListener();

  // imgINotificationObserver methods:
  virtual void Notify(int32_t aType,
                      const mozilla::gfx::IntRect* aRect = nullptr) override;
  virtual void OnLoadComplete(bool aLastPart) override;

  // Other, internal-only methods:
  virtual void SetHasImage() override;

  // Whether we want notifications from ProgressTracker to be deferred until
  // an event it has scheduled has been fired and/or validation is complete.
  virtual bool NotificationsDeferred() const override {
    return IsValidating() || mPendingNotify;
  }
  virtual void MarkPendingNotify() override { mPendingNotify = true; }
  virtual void ClearPendingNotify() override { mPendingNotify = false; }
  bool IsValidating() const { return mValidating; }
  void MarkValidating();
  void ClearValidating();

  // Flags this image load as not cancelable temporarily. This is needed so that
  // stylesheets can be shared across documents properly, see bug 1800979.
  void SetCancelable(bool);

  // Removes all animation consumers that were created with
  // IncrementAnimationConsumers. This is necessary since we need
  // to do it before the proxy itself is destroyed. See
  // imgRequest::RemoveProxy
  void ClearAnimationConsumers();

  nsresult SyncClone(imgINotificationObserver* aObserver,
                     Document* aLoadingDocument, imgRequestProxy** aClone);
  nsresult Clone(imgINotificationObserver* aObserver,
                 Document* aLoadingDocument, imgRequestProxy** aClone);
  already_AddRefed<imgRequestProxy> GetStaticRequest(
      Document* aLoadingDocument);

  imgRequest* GetOwner() const;

  struct LCPTimings {
    bool AreSet() const { return mLoadTime.isSome() && mRenderTime.isSome(); }

    void Reset() {
      mLoadTime = mozilla::Nothing();
      mRenderTime = mozilla::Nothing();
    }

    mozilla::Maybe<mozilla::TimeStamp> mLoadTime;
    mozilla::Maybe<mozilla::TimeStamp> mRenderTime;

    void Set(const mozilla::TimeStamp& aLoadTime,
             const mozilla::TimeStamp& aRenderTime) {
      mLoadTime = Some(aLoadTime);
      mRenderTime = Some(aRenderTime);
    }
  };

  LCPTimings& GetLCPTimings() { return mLCPTimings; }

  const LCPTimings& GetLCPTimings() const { return mLCPTimings; }

 protected:
  friend class mozilla::image::ProgressTracker;
  friend class imgStatusNotifyRunnable;

  class imgCancelRunnable;
  friend class imgCancelRunnable;

  class imgCancelRunnable : public mozilla::Runnable {
   public:
    imgCancelRunnable(imgRequestProxy* owner, nsresult status)
        : Runnable("imgCancelRunnable"), mOwner(owner), mStatus(status) {}

    NS_IMETHOD Run() override {
      mOwner->DoCancel(mStatus);
      return NS_OK;
    }

   private:
    RefPtr<imgRequestProxy> mOwner;
    nsresult mStatus;
  };

  /* Remove from and forget the load group. */
  void RemoveFromLoadGroup();

  /* Remove from the load group and re-add as a background request. */
  void MoveToBackgroundInLoadGroup();

  /* Finish up canceling ourselves */
  void DoCancel(nsresult status);

  /* Do the proper refcount management to null out mListener */
  void NullOutListener();

  // Return the ProgressTracker associated with mOwner and/or mImage. It may
  // live either on mOwner or mImage, depending on whether
  //   (a) we have an mOwner at all
  //   (b) whether mOwner has instantiated its image yet
  already_AddRefed<ProgressTracker> GetProgressTracker() const;

  nsITimedChannel* TimedChannel();

  already_AddRefed<Image> GetImage() const;
  bool HasImage() const;
  imgCacheValidator* GetValidator() const;

  nsresult PerformClone(imgINotificationObserver* aObserver,
                        Document* aLoadingDocument, bool aSyncNotify,
                        imgRequestProxy** aClone);

  virtual imgRequestProxy* NewClonedProxy();

 public:
  NS_FORWARD_SAFE_NSITIMEDCHANNEL(TimedChannel())

 protected:
  mozilla::UniquePtr<ProxyBehaviour> mBehaviour;

 private:
  friend class imgCacheValidator;

  void AddToOwner();
  void RemoveFromOwner(nsresult aStatus);

  nsresult DispatchWithTargetIfAvailable(already_AddRefed<nsIRunnable> aEvent);

  // The URI of our request.
  nsCOMPtr<nsIURI> mURI;

  LCPTimings mLCPTimings;
  // mListener is only promised to be a weak ref (see imgILoader.idl),
  // but we actually keep a strong ref to it until we've seen our
  // first OnStopRequest.
  imgINotificationObserver* MOZ_UNSAFE_REF(
      "Observers must call Cancel() or "
      "CancelAndForgetObserver() before "
      "they are destroyed") mListener;

  nsCOMPtr<nsILoadGroup> mLoadGroup;

  nsLoadFlags mLoadFlags;
  uint32_t mLockCount;
  uint32_t mAnimationConsumers;
  bool mCancelable : 1;
  bool mCanceled : 1;
  bool mIsInLoadGroup : 1;
  bool mForceDispatchLoadGroup : 1;
  bool mListenerIsStrongRef : 1;
  bool mDecodeRequested : 1;

  // Whether we want to defer our notifications by the non-virtual Observer
  // interfaces as image loads proceed.
  bool mPendingNotify : 1;
  bool mValidating : 1;
  bool mHadListener : 1;
};

inline nsISupports* ToSupports(imgRequestProxy* p) {
  return NS_ISUPPORTS_CAST(imgIRequest*, p);
}

// Used for static image proxies for which no requests are available, so
// certain behaviours must be overridden to compensate.
class imgRequestProxyStatic : public imgRequestProxy {
 public:
  imgRequestProxyStatic(Image* aImage, nsIPrincipal* aImagePrincipal,
                        nsIPrincipal* aTriggeringPrincipal,
                        bool hadCrossOriginRedirects);

  NS_IMETHOD GetImagePrincipal(nsIPrincipal** aPrincipal) override;
  NS_IMETHOD GetTriggeringPrincipal(nsIPrincipal** aPrincipal) override;

  NS_IMETHOD GetHadCrossOriginRedirects(
      bool* aHadCrossOriginRedirects) override;

 protected:
  imgRequestProxy* NewClonedProxy() override;

  // Our principal. We have to cache it, rather than accessing the underlying
  // request on-demand, because static proxies don't have an underlying request.
  const nsCOMPtr<nsIPrincipal> mImagePrincipal;
  const nsCOMPtr<nsIPrincipal> mTriggeringPrincipal;
  const bool mHadCrossOriginRedirects;
};

#endif  // mozilla_image_imgRequestProxy_h
