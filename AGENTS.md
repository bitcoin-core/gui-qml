## User interface

Before designing, building or reviewing user-facing QML:

1. Read [design.md](design.md). It is the design and interaction contract.
2. Find the shared controls and components you will reuse, in
   [qml/controls](qml/controls) and [qml/components](qml/components), and look
   at them on the design system page. Name them in your plan.
3. Keep model semantics, validation, persistence, availability, translations
   and lifecycle behavior unless the change is explicitly about them.
4. Build with shared controls and Theme roles and tokens. If a behavior you
   need is missing, improve the shared control instead of styling one page.
5. Check the states listed under "Verify" in design.md, and report what you
   checked and what you couldn't.

Don't take an existing page as design authority; older pages may predate
design.md. When a correction from a reviewer is a reusable rule, change the
shared control or propose a focused addition to design.md. Keep page-only
exceptions local and commented.
