# Bitcoin Core App design

How the Bitcoin Core App looks and behaves, and how to build UI that fits it.
Read this before designing, building or reviewing any user-facing QML. It
states principles and rules; exact values live in code.

## Sources of truth

- [Theme.qml](qml/controls/Theme.qml) owns every color role, text role,
  spacing, radius, size and motion token. Palettes live in
  [qml/themes](qml/themes).
- [SizeClass.qml](qml/controls/SizeClass.qml) owns the compact, regular and
  wide breakpoints.
- The shared controls in [qml/controls](qml/controls) own interaction and
  presentation. Read the control before reusing it.
- The design system page
  ([SettingsDesignSystem.qml](qml/pages/settings/SettingsDesignSystem.qml),
  Settings > Display > Design system in debug builds) shows every shared
  control in its states, one page per foundation and component group. Use the
  pickers on its first page to check palettes, motion modes, text sizes,
  window widths and the mobile preview.
- [icon-policy.md](doc/icon-policy.md) owns icon assets.
- Tests own behavior that can be checked.

This file owns judgment: what to emphasize, which pattern to reuse and which
mistakes to reject. Do not copy token values into pages or this file. If a
role, token or behavior you need is missing, add it to Theme or the shared
control rather than working around it on one page.

Existing pages are evidence of behavior, not design authority. When an older
page disagrees with this file, follow this file and leave the page's behavior
intact.

## Principles

The app should feel calm, precise and trustworthy. People make consequential,
often irreversible decisions here, so clarity beats novelty.

When requirements compete, protect them in this order:

1. Bitcoin, wallet, node, security, privacy and data semantics.
2. User state, translations, accessibility and established navigation.
3. A clear current task, available actions and consequences.
4. Reuse of shared controls and Theme roles, so equal things look and behave
   alike.
5. Visual refinement.

- A visual change never changes a default, validation rule, persisted value,
  availability or side effect.
- Build hierarchy with content order first, then typography, then spacing and
  alignment, then surfaces, and accent color last.
- Native and cross-platform: it should look at home on macOS, Windows and
  Linux without imitating any one of them or looking like a web page.
- Restraint: no decoration that doesn't carry meaning.

## File structure

- `qml/controls/`: generic building blocks with no knowledge of wallets,
  nodes or peers. Theme, SizeClass, text, buttons, fields, rows, pickers,
  menus, dialogs, banners and the split view. They depend only on Qt and
  other controls, take properties and emit signals.
- `qml/components/`: pieces built from controls for one feature, or that read
  app models. The block clock, coin and fee selection, payment requests,
  transaction flow, node and wallet dialogs, onboarding and settings
  scaffolding.
- `qml/pages/`: screens, grouped by area (`wallet`, `node`, `settings`,
  `onboarding`). Page-only pieces stay next to their page.
- `qml/pages/settings/designsystem/`: the showcase, one file per section,
  each opened as its own page.
- `qml/themes/`: `Palette.qml`, one file per palette, and the type set.

Rules:

- Controls never import `components`. If a control needs something from
  components, it is either not a control or that piece belongs in controls.
- A control does not read app models (`walletController`, `nodeModel` and so
  on). The page or component passes the data in.
- Put something in controls only when it is not tied to one feature. A menu
  for one feature (TransactionActionsMenu, PeerActionsMenu) is a component.
- Register every new QML file in `qml/bitcoin_qml.qrc` and every new QML test
  in `test/qml/bitcoin_qmltests.qrc`.
- Every new control or component gets a showcase entry, or an entry with a
  reason in the exemption list in `test/qml/tst_designsystem.qml`. The test
  fails otherwise.

## Layout

- Pages use a page shell that owns scrolling, width and padding: SettingsPage
  for settings and forms, or the page's NavigationSplitView or PageStack.
- Side padding comes from `Theme.space.pagePadding(width)`. Give padding one
  owner; never add a second inset inside a padded shell.
- Bound reading and form content to the shell's maximum content width. Let
  data-heavy tools (graphs, logs, consoles, transaction data) use the full
  width.
- Spacing comes from `Theme.space`. Each gap says something: labels sit close
  to their values, rows in a group sit close together, and a change of task
  gets visibly more space. Don't add one-off margins to fix a grouping
  problem.
- Align to one left edge per column. Center only focused moments: onboarding
  steps, success, empty and waiting states.
- Give each view one dominant task. If every panel has equal weight, fix the
  hierarchy instead of adding decoration.
- Never fix the height of anything that contains text. Text grows with the
  Text size setting and with translations; let height follow
  `implicitHeight`.

## Responsiveness

- Classify by the width of the container, not the screen or the operating
  system: `SizeClass.isCompact(width)` and `SizeClass.widthClassFor(width)`.
  Wide layouts start at `SizeClass.wideWidthMin`.
- Compact widths get compact page padding, single columns, and labels that
  can wrap. Two-column grids stack below `SizeClass.stackColumnsMax`.
- List and detail views use NavigationSplitView. On regular widths both
  columns show; on compact widths the list shows first, selecting pushes the
  detail, and Back returns to the list with its scroll and selection intact.
- Settings uses the same split view, with the sidebar as the list.
- Text wraps, it doesn't shrink. Elide a single-line label only when the full
  value is available elsewhere (a detail view, a tooltip, the accessible
  name).
- Check every layout at, just below and just above each breakpoint, at the
  smallest and largest text size, and in a long translation. The showcase's
  Device picker (Mobile) lowers the window's minimum width so compact layouts
  can be checked on a desktop.

## Elevation and surfaces

Elevation comes from surface color, not shadows. Don't add drop shadows,
glows or blur.

- `background` is the window and page. `surface1`, `surface2` and `surface3`
  are successive nesting levels: a card on the page is `surface1`, a field or
  card inside it is `surface2`.
- Controls that sit on a surface take `surfaceLevel` and pick their own fill
  from `Theme.color.surface(level)`. Set `surfaceLevel` instead of overriding
  colors.
- Hover, pressed, selected and divider are translucent roles
  (`surfaceHover`, `surfacePressed`, `surfaceSelected`, `surfaceDivider`), so
  they work on any surface. Don't pick a darker neutral by hand.
- Modals sit on `modalOverlay`. Only DialogPopup draws it.
- Surfaces that float over other content (popups, menus, the payment
  request sheet) use SurfaceGradientBorder. Cards inside them are flat: the
  surface2 fill separates them.
- Accent fills are for primary actions and the selected segment of a
  SegmentedPicker. Selection in lists, menus and filters is neutral.
- Every color goes through a semantic role (`accent`, `danger`, `success`,
  `warning`, `info`, `paymentRequest`, `surface1`, and so on). Pages never
  use a hex value, a neutral step for meaning, or a `Theme.dark ? a : b`
  branch.

## Cards and containers

- A FormSection groups related rows on one `surface1` card. Its title sits
  outside the card. Rows inside it share the card and are split by dividers;
  they don't get their own borders or cards.
- Don't nest a card inside a card to group things. Use spacing, a section
  title or a divider. The exception is an inset card on a card or in a popup
  (`Theme.radius.cardInset`), such as a fee summary inside a dialog.
- Corner radius follows the surface type from `Theme.radius`: card, inset
  card, list row, field, popup, sheet, menu, small control. Pills and circles
  use `height / 2`. Highlights inside a rounded surface must fit within its
  corners.
- Lists of selectable rows (activity, coins) share one grouped background
  (GroupedRowBackground) rather than one card per row.
- A surface must communicate grouping, interaction, selection or state.
  If spacing alone separates two things, don't add a surface.

## Navigation

- The main window owns the top-level destinations (wallet activity, send,
  receive, node, peers, settings). Add a top-level destination only for a
  recurring primary task, never for one action.
- A stack (PageStack) owns drill-down: list to detail, section to sub-page.
- NavigationSplitView owns list-and-detail areas (Peers, Settings).
- A wizard (OnboardingView) owns a finite sequence: onboarding, wallet
  creation, import.
- A modal owns a short decision that has to be settled before the task can go
  on. A menu owns a handful of actions on one object. Neither hosts a
  multi-step workflow unless it is a sheet built for one.
- Top-level pages have no Back button; nested pages and flows do. Back
  returns to the previous state, Cancel abandons an unfinished task, Done
  closes a finished or self-contained one. Don't swap these labels.
- Nested pages use NavigationBar2 for their title and Back; settings pages
  use SettingsHeader through SettingsPage.
- Switching destinations keeps the state people expect to come back to:
  scroll position, selection, nested pages. Clear secrets and half-finished
  transactions deliberately.
- Hide a destination that can't work in the current mode. Disable an action
  that is temporarily unavailable and say why when it isn't obvious.

## Components

Reach for the shared control first. Check the showcase for its states.

**Actions**

- PrimaryButton: the one main action of a view, whatever its label. Set
  `destructive` for an action that destroys data or can't be undone. Use
  one per view.
- NeutralButton: secondary actions. `NeutralButton.Large` matches
  PrimaryButton's height for side-by-side actions.
- LinkButton: inline text actions and links.
- IconButton: toolbar and inline icon actions; always with an accessible
  name.
- CloseButton: the close action of every modal and banner. Medium by
  default, Large for full-height sheets.
- OverflowMenuButton opens a ContextMenu of secondary actions. CopyButton
  copies and confirms in place.

**Choices**

- SwitchRow: a setting that applies immediately.
- CheckBox: consent or a choice inside a form that applies on submit.
- SegmentedPicker: two to four short, mutually exclusive options that should
  stay visible.
- PopupPicker: a longer list, or options that need a description.
- ContextMenuPicker and the ContextMenu toggles: choices inside a menu, such
  as filters and grouping.
- OptionButton and choice tiles: a few large, described options, such as
  storage or wallet type.
- Slider and RangeSlider: a value on a scale. Discrete sliders can show tick
  marks and step labels.

**Rows and fields**

- FormRow: a labelled row with a trailing control, a description, or a body
  below. ValueRow for read-only values, LinkRow for a row that navigates,
  ListRow for selectable list items, TextFieldRow for a short value edited in
  place.
- LabeledTextField, LabeledTextArea, PasswordTextField and the Bitcoin
  address and amount fields: labels sit outside the field; placeholders are
  examples, never labels.
- Don't make static values look clickable or clickable rows look static.
  Rows that navigate show a disclosure caret.

**Feedback**

- AlertPopup: every alert, confirmation and error dialog. Tones: default,
  success, warning, error. Actions are AlertAction with a role (normal,
  cancel, neutral, destructive).
- ToastBanner: an inline, page-level message with a tone (info, success,
  warning, error). Bind `shown` to make it fade in and out.
- InfoBanner: a neutral card that explains something and may offer an
  action.
- StatusPill: a short state label with a tint. Not for ordinary metadata.
- Tooltip and HoverToolTip: extra detail on hover. Never the only way to
  reach information or an action.
- Skeleton and SpinningIndicator: loading. Keep the layout's shape while
  loading.

**Menus and dialogs**

- ContextMenu with ContextMenuButton, ContextMenuDivider, ContextMenuToggle
  and ContextMenuPicker. Open it with `openBelow(anchor)` so it stays
  anchored and inside the window.
- DialogPopup: the base of every modal. It owns centering, the overlay,
  surface, transitions, height cap, focus and dismissal. Set `sheet` for a
  modal that hosts a multi-step flow.
- DialogHeader: a modal's title and close button.

**Data**

- AddressLabel: an address with copy and in-place "Copied" feedback.
- SelectableText: technical values people copy (IDs, paths, hashes).
- BitcoinAmountDisplayLabel: amounts, following the user's unit and money
  font.
- QRImage: QR codes, always dark on light.

**Layout**

- FormSection, PageHeading, NavigationBar2, NavigationSplitView, PageStack,
  Separator, StateBackground and FocusBorder.

## Dialogs and feedback

- Every dialog has a specific title, a short message, and actions named for
  their outcome ("Delete request", not "OK", when it deletes something).
- Put Cancel first and the main action last.
- Alerts, input dialogs and busy dialogs never close on a press outside.
  Feature modals may.
- Escape closes a dismissible dialog; in an alert it triggers the cancel
  action. Escape never confirms.
- Confirm before destructive actions and before opening external links
  (ExternalPopup). Don't confirm routine, reversible actions.
- A menu action that fails reports in an AlertPopup.
- Errors that need correcting appear next to the field they concern and keep
  the user's input. Page-level state uses a ToastBanner. Never make a
  transient message the only record of an error.
- Show a restart notice only when a change really needs a restart, next to
  that setting.
- Don't stack modals. Settle one before opening another.

## Motion

- Use `Theme.motion` durations and easing only; never write a duration or an
  easing curve on a page.
- Motion explains a change (a page sliding in, a dialog appearing) or confirms
  an action. It never delays access to content.
- Reduced motion (the platform setting or Display > Accessibility > Reduce
  motion) removes movement: no slides, scaling or travel. Short fades, color
  changes and spinners stay. Use `Theme.motion.travel()`, `fade()` and
  `delay()` and they handle this for you.
- Tests run with motion off, so every animation must finish instantly at a
  zero duration and nothing may depend on an animation running.
- Never animate amounts, addresses or other values people verify.
- Stop animations and timers when their view is hidden.

## Typography and color in use

- All text is CoreText with a `textStyle` from `Theme.text`. Pick the role by
  purpose (title, description, caption, button, menu item, amount, mono),
  never by size. Don't set `font.pixelSize`, a family or a weight on a page.
- Text roles follow the Display > Accessibility > Text size setting.
  Layouts must survive the largest step.
- Use mono roles only for literal values: addresses, transaction IDs, hashes,
  paths, commands, log lines. Never for sentences.
- Amounts use the amount roles so they follow the money font setting.
- Color carries meaning only when paired with a non-color cue: text, an icon
  or a shape.

## Accessibility

The bar is the platform's assistive technology reading the app correctly.

- Every interactive element has the right accessible role, a name and its
  state (checked, selected, disabled). Buttons read as buttons, switches as
  switches, tabs as tabs.
- Icon-only buttons get an `Accessible.name`. Controls inside a FormRow take
  the row's title as their name.
- Decorative items (icons next to text, digits drawn for an animation) set
  `Accessible.ignored: true`.
- Dialogs expose the dialog role with their title as the name.
- Text contrast meets 4.5:1 in every palette; `tst_themecontrast` checks the
  roles. Selection, errors and status never rely on color alone.
- Out of scope for now: live announcements (`Accessible.announce` needs Qt
  6.8).

## Keyboard and focus

- Every interactive control is reachable with Tab, in visual order, and can
  be left with Tab. Hidden, disabled and decorative items are skipped.
  Multi-line fields let Tab move on.
- Buttons, rows, tabs and other non-text controls show the purple
  FocusBorder when focused from the keyboard. Text fields show their neutral
  field halo (FieldBorderRings). Never hide the focus indicator.
- Space, Return and Enter activate a focused button; check boxes and switches
  take Space only. Return in a dialog's field answers with its only primary
  action and never confirms a destructive one.
- Selection and focus are different states and both stay visible at once.
- A modal moves focus inside when it opens, keeps it there, and returns it
  to the control that opened it when it closes. DialogPopup does this; don't
  work around it.
- A destructive action is never the initially focused action.
- Menus, the settings sidebar and the wallet switcher move with the arrow
  keys. New keyboard-selectable lists should too.

## Writing

- Sentence case everywhere in the app. Title Case only on the native menu
  bar.
- "…" ends a label whose action opens more UI before anything happens
  ("Export…").
- One term per concept. Use the established Bitcoin Core terms.
- Every user-facing string goes through `qsTr`. Build whole sentences; never
  join fragments. Units and dates use the locale.
- Labels name the outcome ("Speed up", "Create wallet") rather than a generic
  "Continue" when the outcome can be named.
- A description earns its place. If the label and control make the effect
  clear, leave it out. If the action affects funds, fees, privacy or data,
  explain the consequence plainly.
- No marketing language, jokes or false reassurance.

## High-stakes flows

Sending, receiving, wallet creation, import, backup, migration, encryption,
passphrases, external signing and RPC need stricter clarity.

- Show which wallet is acting whenever the wrong one would matter.
- Show amounts, fees, addresses and transaction IDs exactly and consistently.
  Anything people compare or verify must be fully visible or copyable.
- Keep editing separate from review. The review shows exactly what will be
  submitted.
- State irreversible or costly consequences before commitment, without
  alarm.
- Never show secrets after entry. Clear them when the flow ends or is
  abandoned. Keep other input after a recoverable error.
- Distinguish created, signed, broadcast, in the mempool and confirmed. Don't
  collapse them into one "success".
- Every interruptible step has a safe exit, and cancelling says what it does.

## Theming

Appearance preferences are shared across networks and survive resetting one
network's GUI settings.

- A palette is one file in `qml/themes/` that sets the neutral scale and the
  required semantic roles of `Palette.qml`. Everything else derives from
  those. Add it to `Theme.palettes`.
- Override a derived role in the palette file only when the derived value
  fails contrast or looks wrong.
- Pages don't know which palette is active. Never branch on `Theme.dark` for
  color; add or use a role.

## Qt constraints

- CI builds with Qt 6.2, 6.4 and 6.10 and treats warnings as errors. Use no
  QML or Qt API newer than 6.2 without a version guard (no `Qt.alpha()`, no
  `Accessible.announce()`).
- Don't put a Repeater directly inside a Quick Layout (RowLayout,
  ColumnLayout, GridLayout); wrap it in a Column, Row or Flow. Qt 6.4 can
  crash when the layout rearranges while the Repeater rebuilds.
- Avoid binding loops and load warnings; the showcase test fails on any QML
  warning on Qt 6.3+.
- Give interactive and verifiable elements an `objectName` so tests and the
  [test bridge](doc/test-bridge.md) can find them.

## Do and don't

Do:

- Reuse a shared control, or improve it, before writing a new one.
- Use Theme roles and tokens for every color, text style, spacing, radius,
  size and duration.
- Pass data into controls through properties; keep model access in pages and
  components.
- Let one PrimaryButton carry the main action of each view.
- Keep labels outside fields and next to their values.
- Check compact and regular widths, both palettes, reduced motion, the
  largest text size and keyboard navigation before calling UI done.

Don't:

- Hard-code colors, font sizes, line heights, padding, radii or durations.
- Build a control that looks slightly different from an existing one but
  does the same job.
- Nest cards to create grouping, or wrap every value in a card.
- Add shadows, gradients, glows, glass effects or decorative imagery.
- Use pills or badges for ordinary metadata.
- Shrink text to make something fit.
- Rely on hover, color alone or a tooltip for anything essential.
- Fix the height of anything that holds text.
- Give a sparse form a full-width layout, or squeeze a log or graph into a
  narrow column.
- Keep timers, polling or animations running for hidden views.
- Use `Theme.dark` to choose a color.
- Import `components` from a control.

## Verify

Before handing UI work over:

- Run the QML tests (`bitcoinqml_qmltests`) and the functional tests for the
  affected flows. Add a test when a generic test for the behavior is missing.
- Open the changed view in the app. On the design system page, check the
  changed controls with each palette, each motion mode, the smallest and
  largest text size, and the mobile preview.
- Tab through the view and check focus order, focus rings and Escape.
- Say what you checked and what you couldn't check. A passing build is not a
  visual check, and a QML mock test is not a real node.

## Keeping this file current

- Add a rule when a decision is made or the same correction comes up twice.
  Write it as something you can check, not as a feeling.
- Put mechanics in Theme or a shared control, and checkable behavior in a
  test, rather than in prose here.
- Keep a one-off exception next to the code that needs it, with a comment
  saying why.
- When code and this file disagree, fix whichever is wrong in the same
  change.
