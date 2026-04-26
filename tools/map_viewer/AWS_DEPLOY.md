# AWS deployment

The viewer is hosted as a plain static site on S3.

- **Bucket:** `scion-viewer` (us-east-1)
- **Live URL:** http://scion-viewer.s3-website-us-east-1.amazonaws.com/
- **Hosting mode:** S3 static website (HTTP only, no CloudFront)
- **Bucket policy:** public-read on `s3://scion-viewer/*`
- **Website config:** index doc + error doc both `index.html`
- **Contents:** ~7048 objects / ~666 MiB (191 maps, 281 deduped models, glTFs + .bin + PNG)

The Python server in `server.py` is the local dev server only; it is not
deployed anywhere. The browser fetches everything directly from S3.

## URL layout in the bucket

```
/index.html                           map viewer
/models/index.html                    model/animation viewer
/scenes.json                          [{scene, file, url}]
/models-index.json                    {model: {sha6, scenes[], aid_count, ...}}
/maps/<scene>/<file>.gltf|.bin|.png   per-scene map assets (mirrors out/map_gltf_all)
/models/<grp>/aid<N>/<file>           per-model glTF sets (mirrors out/models)
/models/<grp>/_scenes.json            per-model scene manifest
```

S3 static website hosting auto-resolves `/models/` to `/models/index.html`.

## How to redeploy

After regenerating `out/map_gltf_all/` and/or `out/models/` (see
[REGENERATE.md](REGENERATE.md)), just run:

```bash
python -m tools.map_viewer.deploy_aws --bucket scion-viewer
```

The script:

1. Builds `scenes.json` from `out/map_gltf_all/`.
2. Uploads `scenes.json` and `out/models/_index.json` (renamed to
   `models-index.json`) to the bucket root with short cache headers.
3. `aws s3 sync --delete`s `out/map_gltf_all/` → `s3://scion-viewer/maps/`
   and `out/models/` → `s3://scion-viewer/models/` with long immutable
   cache headers.
4. Uploads `index.html` → `/index.html` and `index_anim.html` →
   `/models/index.html` **after** the syncs (so the `--delete` on
   `models/` doesn't remove the model viewer page).
5. Re-stamps every `*.gltf` object with `Content-Type: model/gltf+json`
   (the AWS CLI's default mimetype map doesn't know that extension).

Pass `--dry-run` to preview without executing, or `--skip-fixup` to skip
the gltf Content-Type pass.

## Smoke test after deploy

```bash
URL=http://scion-viewer.s3-website-us-east-1.amazonaws.com
curl -s  $URL/scenes.json        | python -c "import sys,json; print(len(json.load(sys.stdin)),'scenes')"
curl -s  $URL/models-index.json  | python -c "import sys,json; print(len(json.load(sys.stdin)),'models')"
curl -sI $URL/maps/s01_e011/map_0001.gltf       | head -3
curl -sI $URL/models/grp_0001/aid0/grp_0001.gltf | head -3
```

Expect 200, 191 scenes, 281 models, and `Content-Type: model/gltf+json`
on the .gltf objects.

## One-time bucket setup (already done; recorded for the record)

```bash
# Create the bucket (us-east-1 omits LocationConstraint).
aws s3api create-bucket --bucket scion-viewer

# Allow a public bucket policy.
aws s3api put-public-access-block --bucket scion-viewer \
    --public-access-block-configuration \
    "BlockPublicAcls=false,IgnorePublicAcls=false,BlockPublicPolicy=false,RestrictPublicBuckets=false"

# Apply public-read policy.
cat > /tmp/policy.json <<'EOF'
{
  "Version": "2012-10-17",
  "Statement": [{
    "Sid": "PublicReadGetObject",
    "Effect": "Allow",
    "Principal": "*",
    "Action": "s3:GetObject",
    "Resource": "arn:aws:s3:::scion-viewer/*"
  }]
}
EOF
aws s3api put-bucket-policy --bucket scion-viewer --policy file:///tmp/policy.json

# Enable website hosting (index + error doc both index.html so subdir
# indexes resolve cleanly).
aws s3api put-bucket-website --bucket scion-viewer --website-configuration \
    '{"IndexDocument":{"Suffix":"index.html"},"ErrorDocument":{"Key":"index.html"}}'
```
