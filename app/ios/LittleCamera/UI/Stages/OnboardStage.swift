import SwiftUI

/// No blog on this camera yet. Make one or sign in to one, let little camera photograph
/// the code on this screen, then let it take the picture that heads the blog.
struct OnboardStage: View {
    let model: AppModel

    /// One question per screen, in the order the server needs the answers. The address comes before
    /// the blog is named, because nothing is written down until someone has opened the link sent to it
    /// (§4.9): a sign-up left halfway leaves no blog behind, and an address that already owns one is
    /// handed back into it instead of being told it is taken.
    enum Step: Equatable {
        case choose
        case email
        case sent
        case name
        case handle
        case avatar
        case done
    }

    @State private var step: Step = Demo.step ?? .choose
    @State private var name = Demo.typedName
    @State private var email = Demo.typedEmail
    @State private var handle = Demo.typedHandle

    var body: some View {
        DeviceStage(
            title: title,
            mood: mood,
            turn: turn,
            // The question is whether to have a blog at all, so the answer is on the screen behind it.
            phone: step == .choose,
            back: back,
            photo: model.camera.avatarPNG
        ) {
            content
            if let error = model.blog.error {
                Pill(error).padding(.top, 10)
            }
        } actions: {
            buttons
        }
        .onChange(of: model.blog.isLinked) { _, linked in
            if linked { step = .avatar }
        }
        .onChange(of: model.blog.takenEmail) { _, taken in
            if taken { step = .email }
        }
        // An address confirmed on a previous run of the app is still confirmed, so a phone that was
        // closed between the mail and the naming picks up at the name rather than asking again.
        .onChange(of: model.blog.confirmedEmail) { _, confirmed in
            if confirmed != nil, step == .choose || step == .email || step == .sent { step = .name }
        }
        .task {
            if model.blog.confirmedEmail != nil, step == .choose { step = .name }
        }
        .onChange(of: model.camera.avatarPNG) { _, taken in
            if taken != nil, step == .avatar {
                model.celebrate()
                step = .done
            }
        }
        .onChange(of: step) { _, now in
            if now == .handle, handle.isEmpty { handle = Handle.filter(name) }
            if now == .avatar { model.blog.requestAvatar() }
            model.camera.prompt(for: now)
        }
        .task { model.camera.prompt(for: step) }
        // Which link the owner was sent is the server's business, so the phone waits for either to
        // land: a confirmed address to name a blog for, or a claim on a blog that already exists.
        .task(id: step) {
            while step == .sent, !Task.isCancelled {
                try? await Task.sleep(for: .seconds(3))
                model.refresh()
                if model.blog.confirmedEmail != nil { step = .name }
            }
        }
        // Nothing else is watching for the selfie, and the camera will not announce it.
        .task(id: step) {
            while step == .avatar, !Task.isCancelled {
                try? await Task.sleep(for: .seconds(3))
                model.camera.catchUp()
            }
        }
    }

    private var turn: Int {
        (model.blog.isLinked ? 1 : 0) + (step == .done ? 1 : 0)
    }

    private var mood: DeviceMood {
        if model.blog.error != nil { return .alert }
        switch step {
        // A blog is what all of this is for, so the panel shows one rather than a camera.
        case .choose, .email, .name, .handle: return .blog
        case .sent: return .mail
        case .avatar: return .person
        case .done: return .photo
        }
    }

    private var title: String {
        switch step {
        case .choose: return "Start a little blog"
        case .email: return model.blog.takenEmail ? "We\u{2019}ve met before" : "Email please"
        case .name: return "Name please"
        case .handle: return "Claim your spot on the web"
        case .sent: return "Open the email"
        case .avatar: return "Selfie time"
        case .done: return "Your little blog is ready"
        }
    }

    private var back: (() -> Void)? {
        switch step {
        case .choose, .avatar, .done: return nil
        case .email, .sent: return { step = .choose }
        // Back out of naming and the address is still confirmed, so it comes back here, not to the mail.
        case .name: return { step = .choose }
        case .handle: return { step = .name }
        }
    }

    @ViewBuilder private var content: some View {
        switch step {
        case .choose:
            Text("Get started with a new little blog\nor continue one you already have.")
        case .name:
            Text("It goes under every picture you send.")
            // Typed in place against what it will read as, so there is no doubt that the
            // answer is a name rather than a title.
            SuffixField(text: $name, placeholder: "your name", suffix: "\u{2019}s little camera")
        case .email:
            Text(model.blog.takenEmail
                 ? "That address already has a little blog. Ask for a link and carry on with it."
                 : "Stays private, but needed to log in later.")
            Field(placeholder: "you@example.com", text: $email, keyboard: .emailAddress, autofocus: true)
            Terms(server: model.serverURL)
        case .handle:
            AddressField(
                handle: handleBinding,
                suffix: ".\(model.serverURL.withoutScheme)",
                availability: model.blog.availability
            )
            .task(id: handle) {
                try? await Task.sleep(for: .milliseconds(400))
                await model.blog.checkHandle(handle)
            }
        case .sent:
            Text("The link inside is valid for an hour")
            Waiting(text: "Waiting for you")
        case .avatar:
            Text("Point it at yourself and press the button. This will be your profile picture.")
            Waiting(text: "Waiting for the picture")
        case .done:
            Text("Have fun with your little camera")
        }
    }

    @ViewBuilder private var buttons: some View {
        switch step {
        case .choose:
            // One door now: the address decides whether this is a new blog or one already out there,
            // and the owner is never asked to know which.
            Button { step = .email } label: { Caption("Make a little blog") }
                .buttonStyle(BlackButton())
            Button { step = .email } label: { Caption("I already have one") }
                .buttonStyle(QuietButton())
        case .email:
            primary("Email me a link", enabled: email.trimmed.contains("@")) {
                model.blog.useAddress(email)
                step = .sent
            }
        case .name:
            primary("Next", enabled: !name.trimmed.isEmpty) { step = .handle }
        case .handle:
            primary("Make it", enabled: model.blog.availability == .free) {
                model.blog.createBlog(name: name, handle: handle)
            }
        case .sent:
            // The link is in a mail app, so the useful button is the one that opens it. Nothing else
            // to press: clicking the link is what moves this on, and the app watches for it.
            if let mail = URL(string: "message://") {
                Link(destination: mail) { Caption("Open email") }
                    .buttonStyle(BlackButton())
            }
        case .avatar:
            primary("Done") { model.skipAvatar() }
        case .done:
            primary("Go to blog") { model.openBlog() }
        }
    }

    /// Anything the address cannot contain never arrives, so there is nothing to tell anyone off about.
    private var handleBinding: Binding<String> {
        Binding(get: { handle }, set: { handle = Handle.filter($0) })
    }

    private func primary(_ label: String, enabled: Bool = true, action: @escaping () -> Void) -> some View {
        Button(action: action) { Caption(label) }
            .buttonStyle(BlackButton())
            .disabled(!enabled)
    }

}

/// What is typed and what is fixed, on one line, set at the size of the answer. The fixed
/// part is the point: it says what the typed part will read as before anyone commits to it.
struct SuffixField: View {
    @Binding var text: String
    let placeholder: String
    let suffix: String
    @FocusState private var focused: Bool

    var body: some View {
        VStack(spacing: 4) {
            HStack(spacing: 0) {
                TextField(placeholder, text: $text)
                    .font(Theme.font(26, bold: true))
                    .kerning(-0.5)
                    .fixedSize()
                    .textFieldStyle(.plain)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                    .tint(Theme.ink)
                    .focused($focused)
                    .task { focused = true }
                Text(suffix)
                    .font(Theme.font(26))
                    .kerning(-0.5)
            }
            .frame(maxWidth: .infinity)
            Theme.ink.frame(height: 2)
        }
    }
}

/// The address, with the answer to "is it free" under it rather than after the fact.
private struct AddressField: View {
    @Binding var handle: String
    let suffix: String
    let availability: Blog.Availability

    var body: some View {
        VStack(spacing: 0) {
            SuffixField(text: $handle, placeholder: "you", suffix: suffix)
            verdict
                .frame(height: 28)
                .padding(.top, 16)
        }
    }

    @ViewBuilder private var verdict: some View {
        switch availability {
        case .unknown:
            Color.clear
        case .checking:
            HStack(spacing: 10) {
                Spinner()
                Text("Checking").font(Theme.body)
            }
        case .free:
            mark("checkmark", "Available")
        case .taken:
            mark("xmark", "Taken")
        }
    }

    private func mark(_ symbol: String, _ label: String) -> some View {
        HStack(spacing: 8) {
            Image(systemName: symbol).font(.system(size: 15, weight: .bold))
            Text(label).font(Theme.body)
        }
    }
}

/// The small print, which has to be somewhere and belongs where the address is handed over.
private struct Terms: View {
    let server: String

    var body: some View {
        let root = server.hasSuffix("/") ? String(server.dropLast()) : server
        Text("By making a little blog you agree to the [terms](\(root)/terms) and the [privacy notice](\(root)/privacy).")
            .font(Theme.small)
            .multilineTextAlignment(.center)
            .tint(Theme.ink)
            .padding(.top, 6)
    }
}
