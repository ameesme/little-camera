import SwiftUI

/// What the blog holds: the pictures with what people said under them, who reads it, and the way out.
struct BlogTab: View {
    let model: AppModel
    @State private var showingPeople = false
    @State private var newEmail = ""
    @State private var newName = ""

    private var blog: Blog { model.blog }

    var body: some View {
        NavigationStack {
            Group {
                if blog.photos.isEmpty {
                    empty
                } else {
                    feed
                }
            }
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button { showingPeople = true } label: {
                        Label(blog.waiting.isEmpty ? "People" : "\(blog.waiting.count) waiting",
                              systemImage: blog.waiting.isEmpty ? "person.2" : "person.2.badge.plus")
                    }
                }
                if let url = blog.bound.flatMap({ URL(string: $0.url) }) {
                    ToolbarItem(placement: .topBarTrailing) {
                        ShareLink(item: url) { Image(systemName: "square.and.arrow.up") }
                    }
                }
            }
            .sheet(isPresented: $showingPeople) { people }
        }
    }

    private var feed: some View {
        ScrollView {
            LazyVStack(alignment: .leading, spacing: 0) {
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
    }

    /// A blog with nothing on it is the camera waiting to be pressed, not an error.
    private var empty: some View {
        VStack(spacing: 0) {
            Spacer(minLength: 0)
            Device(mood: .viewfinder)
            Text("Nothing here yet")
                .font(Theme.font(30, bold: true))
                .kerning(-1)
                .padding(.top, 28)
            Text("Press the button on little camera and the picture lands here.")
                .font(Theme.lead)
                .multilineTextAlignment(.center)
                .padding(.horizontal, Theme.pad)
                .padding(.top, 10)
            Spacer(minLength: 0)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Ground())
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

