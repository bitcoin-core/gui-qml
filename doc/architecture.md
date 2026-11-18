# Application object model

This describes how the QML application's C++ objects are divided and how the
QML views use them. Follow it for new code. Rename or split an existing class
when a change already touches it; don't rename for its own sake.

## Vocabulary

| Term | Meaning |
|---|---|
| **Model** | Data-backed: domain data, item rows or drafts, with change notifications. It lives as long as its data and holds no page state. |
| **Controller** | State and actions for one use case. QML binds to its state and calls its actions. An *app controller* coordinates across the application (`WalletQmlController`); a *page controller* belongs to one page and is declared by it. |
| **Operation** | One domain request with a lifecycle. It keeps its final state once finished. |
| **Interface** | An abstract class a controller depends on to reach the wallet or node. Production code implements it; tests use a fake. Name it with an `Interface` suffix. |

To place a class, ask: is it data that outlives a page? Then it is a model.
Is it the state and actions of something the user does? Then it is a
controller.

The QML pages and components are the views.

## Object tree

```
ApplicationModels                      composition root
├─ app controllers                     WalletQmlController, DesktopTrayIconController
│   └─ WalletQmlController             the wallet list: opening, selecting, closing
│       ├─ creation                    WalletCreationController: create, restore, migrate
│       └─ names                       WalletNamesController: display names, wallet folder
├─ app models                          NodeModel, ChainModel, PeerListModel, OptionsQmlModel, …
└─ WalletQmlModel (one per wallet)     wallet data; implements the wallet interfaces
    ├─ data models                     activity, addresses, coins, receive requests
    ├─ drafts                          send recipients, fee selection
    └─ wallet-wide state               balances, the busy state, effects after a commit

QML page
└─ page controller                     declared by the page
    ├─ uses interfaces                 implemented by WalletQmlModel or NodeModel
    └─ owns operations                 the requests its page starts
```

## Execution and shutdown

The application owns one `execution::Executor`. Each owner uses its own
`SerialQueue`s on the shared workers. Core runs ordinary node and wallet
work. Priority runs node interruption, shutdown polling, and an explicit
console stop. Rpc runs ordinary console commands. Utility handles outcomes,
file work, and cleanup, with separate workers for signer discovery and for
releasing native wallets, which can wait for Core.

`QmlInitExecutor` owns one thread for `appInitMain()`, its subscription
cleanup, and `appShutdown()`. This leaves Core available for accepted
settings writes during initialization. The payment-link transport owns one
Qt thread for its local server, sockets, and timers. Its request owner uses
shared Utility.

On quit, the application closes admission and cancels waiting prompts.
It waits for settings and startup work and joins the payment-link thread
before interrupting the node on Priority. Settings use Priority and UI
preferences use Utility, so neither waits for a wallet load that holds Core
until the interrupt. It then drains the remaining
features before running `appShutdown()` and joining the node lifecycle
thread. After destroying the QML engine and models, it flushes deferred
deletions, joins the shared executor, and destroys the native interfaces
on a final thread. Qt continues processing events during the final joins.

## Rules

1. **Dependencies point one way:** view → controller → interface ← model.
   Controllers don't know about views. Models don't know about controllers
   or page state.
2. **Each kind of state has one home.** Data and drafts live in models.
   A page's state lives in its controller: the current request, prompts,
   errors and results. QML holds only presentation: layout, focus, animation.
3. **Controllers own the operations their page starts.** A controller has at
   most one active operation of each kind; starting another replaces it.
   Hiding the page or changing its wallet cancels it. Work past the point of
   no return (a commit) holds its own reference and finishes; effects on the
   wallet after it are the wallet's, reached through an interface.
4. **Identity is the object.** An operation captures the objects it acts on
   when it starts. A late result applies to those objects, so no request
   ids, tickets or epochs are needed to tell results apart. A result for an
   object that is no longer shown simply goes unseen.
5. **Each operation reports its own outcome and error.** Don't share one error
   or state slot between different actions.
6. **Tests use fakes of the layer below.** Models run against fake Core
   interfaces, controllers against fake app interfaces, and QML tests use the
   real controllers with fake interfaces rather than mock controllers.

## Scopes

Every page belongs to one scope: a wallet, the node, or the app.

1. **A wallet page acts on one wallet for its whole life.** It receives the
   wallet when it is created and never another. A view that shows the
   selected wallet creates a new page for each wallet instead of re-pointing
   one (`WalletSettingsHost` creates a `WalletSettingsStack` per wallet).
2. **A wallet page's controller derives from `WalletPageController`.** It is
   `active` while its page is shown and `available` while its wallet can be
   used. When either turns false, or the controller is destroyed, it stops:
   prompts and work the backend has not admitted end; admitted work finishes
   on its wallet and keeps its outcome until the page dismisses it. A
   controller is bound to one wallet; a second binding is rejected and
   disables it until its page is recreated.
3. **The wallet exposes data and wallet-wide state, never an operation shared
   between pages.** Its interfaces (`WalletInterface` subclasses) start a new
   operation for the controller that asks. Wallet-wide limits and unresolved
   effects (a backup still writing, a fee bump whose outcome is unknown) are
   kept by the wallet until the backend resolves them, not by the page, so
   they survive the page; a second start is refused on the second
   controller's own state.
4. **Loading or unloading one wallet never recreates another wallet's
   pages.**

`WalletQmlModelSurfaceTests` checks that `WalletQmlModel` gains no new wallet
actions: its list of invokables and operation properties only shrinks.

## Declarative QML

Pages bind to controller and model state: text, enabled, busy, errors, which
prompt is shown, which step of a flow is shown. Pages don't copy or synchronize
that state, keep tickets, or clean up requests on visibility or wallet
changes; the controller's `wallet` and `active` properties do that.

`AlertPopup` follows a bound `shown` property: bound, it never closes itself
and reports Cancel, Escape and closing actions as `closeRequested()`, so a
prompt's visibility can be a binding to a controller's state.

The QML lint (`cmake --build <build> --target qml_lint`) rejects writable QML
properties named like a request ticket, token or epoch (`app.request-ticket`):
a page that stores a request identity to match a later result is doing its
controller's work.

## User actions

- An action that changes data or starts work calls a controller slot from a
  one-line handler. Pass values only the view has as arguments: field text,
  a chosen file path, drop or paste data, and a stable id for a list row,
  never its index.
- Opening native UI (file dialogs, menus, share sheets), navigation (back,
  tabs, pushing a page) and focus stay in the view. Drive them from
  controller state where possible, such as a page chosen by a result, or from
  a controller signal.
- An action that crosses controllers goes through an app controller or a
  model, for example routing an opened URI to a wallet's send draft. One page
  controller never calls another through QML.
- Keys and shortcuts map to the same action as the matching button, with no
  logic of their own. Shared controls decide when Return and Escape apply.
- Work that must wait for a transition stays in the view.

## Example: sending a reviewed transaction

```
WalletQmlModel
 ├─ draft                 SendRecipientsListModel, fees
 └─ currentReview         TransactionReview (model)

SendDraftController       preparation or PSBT import produces a new review
TransactionReviewController
 ├─ review: wallet.currentReview          a binding, not a copy
 └─ operation                             send, sign only, save PSBT,
                                          broadcast, recheck, signer approval
```

A `TransactionReview` holds the reviewed transaction, its PSBT, fee proof,
source and the draft revision it was built from, and derives `canSend`,
`canBroadcast` and `stale`. Only signing and signer approval change a
review's contents; any other change makes a new review. The review controller
reacts in one place when `currentReview` changes: an operation of the old
review that is waiting at a prompt is cancelled.
