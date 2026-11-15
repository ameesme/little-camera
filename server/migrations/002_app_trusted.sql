-- The QR verification picture is gone: a camera is bound when its owner names a blog or opens the
-- emailed link, and whoever holds the camera's secret manages the blog it is bound to.

-- A phone that was let in ahead of the QR photo was only ever waiting to be bound.
UPDATE photos SET blog_id = (SELECT claim_blog_id FROM cameras WHERE cameras.id = photos.camera_id)
 WHERE blog_id IS NULL
   AND camera_id IN (SELECT id FROM cameras WHERE blog_id IS NULL AND claim_blog_id IS NOT NULL);
UPDATE cameras SET blog_id = claim_blog_id, bound_at = claim_approved_at
 WHERE blog_id IS NULL AND claim_blog_id IS NOT NULL;

DROP TABLE verification_codes;
ALTER TABLE cameras DROP COLUMN claim_approved_at;
ALTER TABLE cameras DROP COLUMN claim_blog_id;
