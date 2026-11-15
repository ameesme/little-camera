-- Motion photos (docs/protocol.md §2.1): how many frames the stored PBM holds and how long each shows.
-- Recorded at upload so the feed can say so without reading every file; a still is 1 frame and no interval.
ALTER TABLE photos ADD COLUMN frames INTEGER NOT NULL DEFAULT 1;
ALTER TABLE photos ADD COLUMN frame_ms INTEGER;
