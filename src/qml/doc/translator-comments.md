# Translator Comment Policy

Translator comments are required for all user-facing strings. A PR introducing
new user-facing strings must also submit the accompanying translator comment.
To prevent this requirement from halting development, the PR author can specify
that they would like the introduction of the comment to be picked up in a
follow-up PR.

## Formulating Translator Comments

Translator comments only apply to strings that are marked for translation.
For more information, see:
[Internationalization and Localization with Qt Quick](https://doc.qt.io/qt-5/qtquick-internationalization.html)

Below, the values for the `text` and `description` properties are examples of
new user-facing strings marked for translation without translator comments:

```qml
OptionButton {
   ButtonGroup.group: group
   Layout.fillWidth: true
   text: qsTr("Only when on Wi-Fi")
   description: qsTr("Loads quickly when on wi-fi and pauses when on cellular data.")
}
```

Translator comments are needed to provide a translator with the appropriate
context needed to aid them with translating a certain string.
The first step in formulating a translator comment is to think about what
information a translator needs to perform their job effectively.

**Necessary context is**:
- Where is this string shown?
  - e.g. Shown in the Create Wallet dialog, Shown in file menu options ...
- Explain what the string is meant to convey
  - This is an explanatory text shown after X that aims to inform the user that ...
- What action is associated with this string?
  - If the string is associated with a button, what is the outcome of pressing the button?

**Optional context is**:
- Alternatives to complicated/technical words
  - Some words may not have a direct one-to-one translation in certain languages.
    It is nice to provide a replacement for such a word. For example,
    some languages may not have a 1-1 translation for the word "subnet",
    an appropriate substitute for this word is "IP address".

## Writing Translator Comments

Begin by writing down the text for the comment, guided by the necessary and
optional context specified in the previous section. Ensure that your text ends
in a full stop.

For the value of the `text` property, you may write:

> Main text for button displayed when choosing the network mode the client will
run in. Choosing this option means that the node will only sync when the device
is connected to a wireless internet connection.

For the value of the `description` property, you may write:

> Explanatory text for button which allows user to set the client to only load
when connected to wifi. This text intends to make it clear to the user that,
under this mode, their node will not sync if connected to cellular data.
If there is no clear translation for "cellular data", use the translation for
"mobile data".

Now take these texts, split them into separate sentences, and append `//:`
to them. Place the text above its respective translatable string:

```qml
OptionButton {
   ButtonGroup.group: group
   Layout.fillWidth: true
   //: Main text for button displayed when choosing the network mode the client will run in.
   //: Choosing this option means that the node will only sync when the device is connected to a wireless internet connection.
   text: qsTr("Only when on Wi-Fi")
   //: Explanatory text for button which allows user to set the client to only load when connected to wifi.
   //: This text intends to make it clear to the user that, under this mode, their node will not sync if connected to cellular data.
   //: If there is no clear translation for "cellular data", use the translation for "mobile data".
   description: qsTr("Loads quickly when on wi-fi and pauses when on cellular data.")
}
```

The special character `//:` lets Qt's translator engine know that the proceeding
text is a translator comment. Each line will get concatenated together to be
presented to the translator as one paragraph.
