import SwiftUI

/// What the blog holds: the pictures with what people said under them, who reads it, and the way out.
struct BlogTab: View {
    let model: AppModel
    @State private var showing: Sheet?
    @State private var newEmail = ""
    @State private var newName = ""

    private enum Sheet: String, Identifiable {
        case people
        case share
        case log
        var id: String { rawValue }
    }

    private var blog: Blog { model.blog }

    var body: some View {
        NavigationStack {
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 0) {
                    if blog.photos.isEmpty {
                        Text("No pictures yet. Take one and press send.")
                            .font(Theme.small)
                            .card()
                    }
                    ForEach(blog.photos) { photo in
                        PhotoPost(photo: photo)
                        Rule()
                    }
                    if blog.nextPage != nil {
                        Button { blog.loadMore() } label: { Caption("Older") }
                            .buttonStyle(BlackButton())
                            .card()
                    }
                }
            }
            .refreshable { model.refresh() }
            .navigationTitle(blog.bound?.handle ?? "Blog")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Menu {
                        Button("People\(blog.waiting.isEmpty ? "" : " (\(blog.waiting.count) waiting)")") { showing = .people }
                        Button("Share link") { showing = .share }
                        Button("Activity") { showing = .log }
                        Divider()
                        // One act, two names: this phone lets go of the camera and the blog together.
                        Button("Log out and disconnect", role: .destructive) { model.disconnect() }
                    } label: {
                        Image(systemName: "ellipsis")
                    }
                }
            }
            .sheet(item: $showing) { sheet in
                switch sheet {
                case .people: people
                case .share: share
                case .log: LogSheet(log: model.log)
                }
            }
        }
    }

    private var people: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    Caption("Add someone")
                    Field(placeholder: "them@example.com", text: $newEmail, keyboard: .emailAddress)
                    Field(placeholder: "Their name (optional)", text: $newName)
                    Button {
                        blog.add(email: newEmail, name: newName)
                        newEmail = ""
                        newName = ""
                    } label: {
                        Caption("Add and let in")
                    }
                    .buttonStyle(BlackButton())
                    .disabled(newEmail.trimmed.isEmpty)

                    if !blog.waiting.isEmpty {
                        Rule()
                        Caption("Asked to follow")
                        ForEach(blog.waiting) { sub in
                            SubscriberRow(subscriber: sub, blog: blog)
                        }
                    }
                    Rule()
                    Caption("Reading along (\(blog.approved.count))")
                    ForEach(blog.approved) { sub in
                        SubscriberRow(subscriber: sub, blog: blog)
                    }
                }
                .card()
            }
            .navigationTitle("People")
            .navigationBarTitleDisplayMode(.inline)
        }
    }

    private var share: some View {
        NavigationStack {
            VStack(alignment: .leading, spacing: 14) {
                Text("Anyone with this address can ask to follow. You decide who gets in.")
                    .font(Theme.small)
                Text(blog.bound?.url.withoutScheme ?? "")
                    .font(Theme.font(17, bold: true))
                if let url = blog.bound.flatMap({ URL(string: $0.url) }) {
                    ShareLink(item: url) {
                        Caption("Share it")
                    }
                    .buttonStyle(BlackButton())
                }
                Spacer()
            }
            .card()
            .navigationTitle("Share")
            .navigationBarTitleDisplayMode(.inline)
        }
    }
}

/// One picture, at the dither's own scale, with whatever was said under it.
private struct PhotoPost: View {
    let photo: FeedPhoto

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            AsyncImage(url: URL(string: photo.url)) { image in
                image
                    .resizable()
                    .interpolation(.none)
                    .aspectRatio(4.0 / 3.0, contentMode: .fit)
            } placeholder: {
                Dither().aspectRatio(4.0 / 3.0, contentMode: .fit)
            }
            .overlay(Rectangle().stroke(Theme.ink, lineWidth: 1))

            Caption(Date(timeIntervalSince1970: TimeInterval(photo.capturedAt)).formatted(date: .abbreviated, time: .shortened))
            ForEach(photo.comments) { comment in
                Text("**\(comment.name)** \(comment.body)")
                    .font(Theme.small)
            }
        }
        .card()
    }
}

private struct SubscriberRow: View {
    let subscriber: Subscriber
    let blog: Blog

    var body: some View {
        HStack(alignment: .firstTextBaseline) {
            VStack(alignment: .leading, spacing: 2) {
                Text(subscriber.name ?? subscriber.email)
                if subscriber.name != nil {
                    Text(subscriber.email).font(Theme.small)
                }
            }
            Spacer(minLength: 8)
            if subscriber.status == "pending" {
                Button { blog.approve(subscriber) } label: { Caption("Let in") }
                    .buttonStyle(BlackButton())
            }
            Menu {
                if subscriber.status != "blocked" {
                    Button("Block") { blog.block(subscriber) }
                } else {
                    Button("Unblock") { blog.approve(subscriber) }
                }
                Button("Remove for good", role: .destructive) { blog.remove(subscriber) }
            } label: {
                Image(systemName: "ellipsis")
            }
        }
    }
}

private struct LogSheet: View {
    let log: SyncLog

    var body: some View {
        NavigationStack {
            ScrollView {
                LogScreen(log: log)
            }
            .navigationTitle("Activity")
            .navigationBarTitleDisplayMode(.inline)
        }
    }
}

extension View {
    /// `sheet(item:)` with a name that reads at the call site.
    func sheet<Item: Identifiable, Content: View>(
        item: Binding<Item?>,
        @ViewBuilder content: @escaping (Item) -> Content
    ) -> some View {
        sheet(isPresented: Binding(get: { item.wrappedValue != nil }, set: { if !$0 { item.wrappedValue = nil } })) {
            if let value = item.wrappedValue { content(value) }
        }
    }
}
