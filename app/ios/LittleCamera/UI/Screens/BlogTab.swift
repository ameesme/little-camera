import SwiftUI

/// What the blog holds: the pictures with what people said under them, who reads it, and the way out.
struct BlogTab: View {
    let model: AppModel
    @State private var showingPeople = Demo.showingPeople

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
                        Label("People", systemImage: "person.2.fill")
                    }
                }
                if let url = blog.bound.flatMap({ URL(string: $0.url) }) {
                    ToolbarItem(placement: .topBarTrailing) {
                        ShareLink(item: url) { Image(systemName: "square.and.arrow.up") }
                    }
                }
            }
            .sheet(isPresented: $showingPeople) { PeopleScreen(blog: blog) }
        }
    }

    private var feed: some View {
        ScrollView {
            LazyVStack(alignment: .leading, spacing: 0) {
                ForEach(blog.photos) { photo in
                    PhotoPost(photo: photo, blog: blog)
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
}

/// One picture, at the dither's own scale, with whatever was said under it.
private struct PhotoPost: View {
    let photo: FeedPhoto
    let blog: Blog
    @State private var confirmingDelete = false

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
        .background(Theme.paper)
        .contextMenu {
            Button { save() } label: { Label("Save to photos", systemImage: "square.and.arrow.down") }
            Button(role: .destructive) { confirmingDelete = true } label: {
                Label("Delete", systemImage: "trash")
            }
        }
        .confirmationDialog("Delete this picture?", isPresented: $confirmingDelete, titleVisibility: .visible) {
            Button("Delete", role: .destructive) { blog.delete(photo) }
        } message: {
            Text("It goes off the blog for everyone, with whatever was said under it.")
        }
    }

    /// The server's PNG, not a redraw of it: the dither is the picture (AGENTS.md).
    private func save() {
        guard let url = URL(string: photo.url) else { return }
        Task {
            guard let (data, _) = try? await URLSession.shared.data(from: url),
                  let image = UIImage(data: data) else { return }
            UIImageWriteToSavedPhotosAlbum(image, nil, nil, nil)
        }
    }
}
