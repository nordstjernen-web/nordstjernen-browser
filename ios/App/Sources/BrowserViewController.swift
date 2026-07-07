// Nordstjernen — top-level browser UI: URL bar, navigation toolbar and the
// engine-backed PageView.

import UIKit

final class BrowserViewController: UIViewController, UITextFieldDelegate, PageViewDelegate {
    private let urlField = UITextField()
    private let pageView = PageView()
    private let toolbar = UIToolbar()

    private var history: [String] = []
    private var historyIndex = -1

    private lazy var backItem = UIBarButtonItem(image: UIImage(systemName: "chevron.backward"),
                                                style: .plain, target: self, action: #selector(goBack))
    private lazy var forwardItem = UIBarButtonItem(image: UIImage(systemName: "chevron.forward"),
                                                   style: .plain, target: self, action: #selector(goForward))
    private lazy var reloadItem = UIBarButtonItem(barButtonSystemItem: .refresh,
                                                  target: self, action: #selector(reload))

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .systemBackground
        BrowserEngine.shared.start()
        setupURLBar()
        setupToolbar()
        setupPageView()
        load("about:start")
    }

    private func setupURLBar() {
        urlField.borderStyle = .roundedRect
        urlField.autocapitalizationType = .none
        urlField.autocorrectionType = .no
        urlField.keyboardType = .URL
        urlField.clearButtonMode = .whileEditing
        urlField.returnKeyType = .go
        urlField.placeholder = "Search or enter address"
        urlField.delegate = self
        urlField.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(urlField)
        NSLayoutConstraint.activate([
            urlField.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor, constant: 8),
            urlField.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: 8),
            urlField.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -8),
            urlField.heightAnchor.constraint(equalToConstant: 40),
        ])
    }

    private func setupPageView() {
        pageView.pageDelegate = self
        pageView.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(pageView)
        NSLayoutConstraint.activate([
            pageView.topAnchor.constraint(equalTo: urlField.bottomAnchor, constant: 8),
            pageView.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            pageView.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            pageView.bottomAnchor.constraint(equalTo: toolbar.topAnchor),
        ])
    }

    private func setupToolbar() {
        toolbar.translatesAutoresizingMaskIntoConstraints = false
        let flex = UIBarButtonItem(barButtonSystemItem: .flexibleSpace, target: nil, action: nil)
        toolbar.items = [backItem, flex, forwardItem, flex, reloadItem]
        view.addSubview(toolbar)
        NSLayoutConstraint.activate([
            toolbar.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            toolbar.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            toolbar.bottomAnchor.constraint(equalTo: view.safeAreaLayoutGuide.bottomAnchor),
        ])
        updateNavButtons()
    }

    // MARK: - Navigation

    private func load(_ input: String, pushHistory: Bool = true) {
        let url = Self.normalize(input)
        urlField.text = url
        urlField.resignFirstResponder()
        let width = pageView.cssViewportWidth
        let height = Double(pageView.bounds.height)
        BrowserEngine.shared.open(url, viewportWidth: width, viewportHeight: height) { [weak self] page in
            guard let self = self else { return }
            guard let page = page else { return }
            if pushHistory { self.pushHistory(page.url ?? url) }
            self.title = page.title
            self.urlField.text = page.url ?? url
            self.pageView.loadPage(width: page.width, height: page.height)
            self.startTicking()
            self.updateNavButtons()
        }
    }

    /// Pump the engine a few times so deferred/animated content settles after open.
    private func startTicking() {
        BrowserEngine.shared.tick { [weak self] _ in
            self?.pageView.setNeedsLayout()
        }
    }

    private func pushHistory(_ url: String) {
        if historyIndex < history.count - 1 {
            history.removeSubrange((historyIndex + 1)...)
        }
        history.append(url)
        historyIndex = history.count - 1
    }

    @objc private func goBack() {
        guard historyIndex > 0 else { return }
        historyIndex -= 1
        load(history[historyIndex], pushHistory: false)
    }

    @objc private func goForward() {
        guard historyIndex < history.count - 1 else { return }
        historyIndex += 1
        load(history[historyIndex], pushHistory: false)
    }

    @objc private func reload() {
        guard historyIndex >= 0 else { return }
        load(history[historyIndex], pushHistory: false)
    }

    private func updateNavButtons() {
        backItem.isEnabled = historyIndex > 0
        forwardItem.isEnabled = historyIndex < history.count - 1
    }

    private static func normalize(_ input: String) -> String {
        let trimmed = input.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.hasPrefix("about:") { return trimmed }
        if trimmed.contains("://") { return trimmed }
        if trimmed.contains(".") && !trimmed.contains(" ") { return "https://" + trimmed }
        let query = trimmed.addingPercentEncoding(withAllowedCharacters: .urlQueryAllowed) ?? trimmed
        return "https://duckduckgo.com/?q=" + query
    }

    // MARK: - UITextFieldDelegate

    func textFieldShouldReturn(_ textField: UITextField) -> Bool {
        if let text = textField.text, !text.isEmpty { load(text) }
        return true
    }

    // MARK: - PageViewDelegate

    func pageView(_ pageView: PageView, didNavigateTo url: String) {
        load(url)
    }

    func pageViewDidUpdateMetadata(_ pageView: PageView) {
        updateNavButtons()
    }
}
