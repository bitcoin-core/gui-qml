// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/controls" as Controls
TestCase {
    id: testCase
    name: "R10PasteBoundaries"
    when: windowShown
    width: 600
    height: 300
    Component {
        id: nativeMultiline
        Controls.LabeledTextArea {
            width: 540
            height: 180
            interceptPaste: true
            property int pasteCalls: 0
            property string capturedText: ""
            onPaymentUriPasted: (text) => {
                pasteCalls += 1
                capturedText = text
            }
        }
    }
    function init() { testSetup.failOnWarnings(); Clipboard.currentText = "" }
    function test_native_multiline_preserves_clipboard_provenance_data() {
        const uri = "bitcoin:bcrt1qtarget"
        const limit = "bitcoin:" + "a".repeat(504)
        return [
            {tag: "empty-exact-limit", previous: "", current: limit, clip: limit, accepted: true},
            {tag: "prefix", previous: "left", current: "left" + uri, clip: uri, accepted: true},
            {tag: "suffix", previous: "right", current: uri + "right", clip: uri, accepted: true},
            {tag: "both", previous: "leftright", current: "left" + uri + "right", clip: uri, accepted: true},
            {tag: "different-prefix", previous: "leftright", current: "other" + uri + "right", clip: uri, accepted: false},
            {tag: "different-suffix", previous: "leftright", current: "left" + uri + "other", clip: uri, accepted: false},
            {tag: "unchanged", previous: uri, current: uri, clip: uri, accepted: false}
        ]
    }
    function test_native_multiline_preserves_clipboard_provenance(data) {
        const editor = createTemporaryObject(nativeMultiline, testCase.Window.window.contentItem,
                                             {paymentUriRestoreText: data.previous})
        verify(editor !== null)
        verify(waitForRendering(editor))
        Clipboard.currentText = data.clip
        verify(testSetup.replaceText(editor.field, data.current))
        compare(editor.pasteCalls, data.accepted ? 1 : 0)
        compare(editor.text, data.accepted ? data.previous : data.current)
        compare(Clipboard.currentText, data.clip)
        if (data.accepted) compare(editor.capturedText, data.clip)
    }

}
