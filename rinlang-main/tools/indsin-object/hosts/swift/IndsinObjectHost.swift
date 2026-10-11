// IndsinObjectHost.swift — iOS / iPadOS / macOS (Catalyst or AppKit-via-WKWebView) host for indsin-object.
// MIT License. Add `indsin_object.html` (built by tools/indsin-object/build.js) to your app bundle.
//
//   let host = IndsinObjectHost(webView: webView, theme: "dark") { event in
//       if event.type == "tap" { engine.tap(x: event.x ?? 0, y: event.y ?? 0) }
//   }
//   host.push(frameJson: json)              // every new Indsin engine frame
//   host.setTheme("auto")
//
// Events arrive through `webkit.messageHandlers.indsin` as plain dictionaries and are never evaluated.

import Foundation
import WebKit

public struct IndsinEvent {
    public let type: String
    public let id: String
    public let path: String
    public let kind: String
    public let x: Double?
    public let y: Double?
    public let value: Any?
    public let raw: [String: Any]
}

public final class IndsinObjectHost: NSObject, WKScriptMessageHandler, WKNavigationDelegate {
    private weak var webView: WKWebView?
    private let onEvent: (IndsinEvent) -> Void
    private var loaded = false
    private var pending: String?

    /// - Parameters:
    ///   - webView: the web view to drive (its configuration's user-content controller receives the "indsin" handler).
    ///   - theme: built-in theme name, "dark", "light" or "auto".
    ///   - rtl: force a text direction, or nil for automatic.
    ///   - bundle: bundle containing `indsin_object.html`.
    public init(webView: WKWebView, theme: String = "dark", rtl: Bool? = nil, bundle: Bundle = .main,
                onEvent: @escaping (IndsinEvent) -> Void = { _ in }) {
        self.webView = webView
        self.onEvent = onEvent
        super.init()
        webView.configuration.userContentController.removeScriptMessageHandler(forName: "indsin")
        webView.configuration.userContentController.add(WeakHandler(self), name: "indsin")
        webView.navigationDelegate = self
        guard let url = bundle.url(forResource: "indsin_object", withExtension: "html") else {
            assertionFailure("indsin_object.html is missing from the bundle"); return
        }
        var comps = URLComponents(url: url, resolvingAgainstBaseURL: false)!
        comps.queryItems = [URLQueryItem(name: "demo", value: "0"), URLQueryItem(name: "theme", value: theme),
                            URLQueryItem(name: "dir", value: rtl == nil ? "auto" : (rtl! ? "rtl" : "ltr"))]
        webView.loadFileURL(comps.url!, allowingReadAccessTo: url.deletingLastPathComponent())
    }

    deinit { webView?.configuration.userContentController.removeScriptMessageHandler(forName: "indsin") }

    /// Renders a new frame (engine envelope or logical tree). Safe to call before the page finished loading.
    public func push(frameJson: String) {
        guard loaded else { pending = frameJson; return }
        run("IndsinObject.applyFrame(\(Self.jsLiteral(frameJson)))")
    }

    public func setTheme(_ theme: String) { if loaded { run("IndsinObject.setTheme(\(Self.jsLiteral(theme)))") } }
    public func setOpen(_ name: String, open: Bool) { if loaded { run("IndsinObject.setOpen(\(Self.jsLiteral(name)), \(open))") } }

    // MARK: WKNavigationDelegate
    public func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
        loaded = true
        if let p = pending { pending = nil; push(frameJson: p) }
    }

    public func webView(_ webView: WKWebView, decidePolicyFor action: WKNavigationAction,
                        decisionHandler: @escaping (WKNavigationActionPolicy) -> Void) {
        guard let url = action.request.url else { return decisionHandler(.cancel) }
        if url.isFileURL { return decisionHandler(.allow) }
        if ["http", "https", "mailto", "tel"].contains(url.scheme ?? "") {
            #if canImport(UIKit)
            UIApplication.shared.open(url)
            #elseif canImport(AppKit)
            NSWorkspace.shared.open(url)
            #endif
        }
        decisionHandler(.cancel)
    }

    // MARK: WKScriptMessageHandler
    public func userContentController(_ controller: WKUserContentController, didReceive message: WKScriptMessage) {
        guard let d = message.body as? [String: Any], let type = d["type"] as? String else { return }
        onEvent(IndsinEvent(type: type, id: d["id"] as? String ?? "", path: d["path"] as? String ?? "",
                            kind: d["kind"] as? String ?? "", x: (d["x"] as? NSNumber)?.doubleValue,
                            y: (d["y"] as? NSNumber)?.doubleValue, value: d["value"], raw: d))
    }

    // MARK: helpers
    private func run(_ js: String) { DispatchQueue.main.async { self.webView?.evaluateJavaScript(js, completionHandler: nil) } }

    /// A string as a JavaScript literal (JSON string encoding is a valid JS string literal).
    static func jsLiteral(_ s: String) -> String {
        guard let data = try? JSONSerialization.data(withJSONObject: [s], options: []),
              var arr = String(data: data, encoding: .utf8) else { return "\"\"" }
        arr.removeFirst(); arr.removeLast()
        return arr.replacingOccurrences(of: "\u{2028}", with: "\\u2028").replacingOccurrences(of: "\u{2029}", with: "\\u2029")
    }

    /// Avoids the WKUserContentController → handler retain cycle.
    private final class WeakHandler: NSObject, WKScriptMessageHandler {
        weak var target: WKScriptMessageHandler?
        init(_ t: WKScriptMessageHandler) { target = t }
        func userContentController(_ c: WKUserContentController, didReceive m: WKScriptMessage) { target?.userContentController(c, didReceive: m) }
    }
}
#if canImport(UIKit)
import UIKit
#elseif canImport(AppKit)
import AppKit
#endif
