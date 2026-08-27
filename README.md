# NppS3

NppS3 is a Notepad++ plugin for working with Amazon S3 and Cloudflare R2 directly from the editor.

You can browse buckets and objects from a docked panel, open remote objects in Notepad++, edit them, and save the changes back to object storage with `Ctrl+S`.

It is useful for making quick changes to configuration files, small scripts, static site files, and similar content stored in S3 or R2. NppS3 is intended as an object storage browser and editor rather than a synchronization or backup tool.

*[한국어 문서](README.ko-KR.md)*

---

## Features

- Browse profiles, object trees, transfers, and activity logs from a docked **browser panel**.
- **Edit remote objects directly.** Open an object, make changes, and press `Ctrl+S` to upload it in the background.
- **Conflict detection** checks the remote ETag immediately before upload. If the object has changed since it was downloaded, NppS3 asks what to do before overwriting it.
- **Prefix browsing** is based on `ListObjectsV2` with `/` as the delimiter. Nodes are loaded on demand, and additional pages are fetched using continuation tokens.
- Large objects use **multipart upload**. Interrupted uploads can be **resumed**, avoiding the 4 GiB single-request limit.
- **Folder download** can download an entire bucket or prefix and recreate the remote hierarchy as a local directory tree.
- Supports upload, download, Save As, delete, rename, move, new file, new folder, key copy, `s3://` URI copy, and object properties.
- **Secret Access Keys are stored in Windows Credential Manager.** Profile files contain only non-secret settings such as the Access Key ID and endpoint.
- The UI is available in **five languages**: English, 한국어, 日本語, 中文, and Русский. It can follow the Notepad++ language setting or be selected manually.
- No AWS SDK or additional runtime DLLs are required. Networking uses WinHTTP, TLS uses Schannel, and hashing uses CNG. The only file required for distribution is `NppS3.dll`.

## Requirements

- Notepad++ for x64, x86, or ARM64
  - The plugin architecture must match the Notepad++ build.
  - Tested with Notepad++ 8.9.1 x64.
  - The oldest Notepad++ API used by NppS3 is `NPPM_ADDTOOLBARICON_FORDARKMODE`, introduced in Notepad++ 8.0.
  - NppS3 will therefore likely work on 8.0 and later, but versions earlier than 8.9.1 have not been tested separately.
- Windows 10 or Windows 11
- To build from source:
  - Visual Studio 2022 or Visual Studio Build Tools with the C++ workload installed
  - CMake 3.21 or later
  - For ARM64 builds, the `MSVC v143 - ARM64 build tools` component

## Manual installation

Notepad++ loads plugins from a directory with the same name as the DLL.

```text
<Notepad++>\plugins\NppS3\NppS3.dll
```

A standard installation usually uses:

```text
%PROGRAMFILES%\Notepad++\plugins\NppS3\
```

For a portable installation:

```text
<portable directory>\plugins\NppS3\
```

Create the `NppS3` directory, place `NppS3.dll` inside it, and restart Notepad++.

If you downloaded the DLL from the Internet, right-click the file and open **Properties**. If an **Unblock** option is shown, enable it and apply the change. Windows may otherwise block the downloaded DLL and prevent Notepad++ from loading the plugin.

## Building

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

For x86, use `-A Win32`. For ARM64, use `-A ARM64`. It is best to keep separate build directories for each architecture.

```powershell
cmake -S . -B build-x86   -G "Visual Studio 17 2022" -A Win32
cmake -S . -B build-arm64 -G "Visual Studio 17 2022" -A ARM64
```

Build outputs:

| File | Description |
| --- | --- |
| `build\Release\NppS3.dll` | Notepad++ plugin |
| `build\Release\npps3_tests.exe` | Unit tests |
| `build\Release\npps3_integration.exe` | Integration tests against a real S3-compatible service |
| `build\Release\npps3_s3cli.exe` | CLI used for host-level testing |

The CRT is linked statically. tinyxml2 and doctest are included under `third_party/`, so there is no separate runtime to install and no additional DLLs to distribute.

## Cloudflare R2 setup

In the Cloudflare dashboard, go to **R2 → Manage R2 API Tokens**, create a token, and enter its credentials in an NppS3 profile.

| Setting | Value |
| --- | --- |
| Provider | Cloudflare R2 |
| Endpoint | `https://<ACCOUNT_ID>.r2.cloudflarestorage.com` |
| Region | `auto` |
| Access Key ID | Access Key ID from the generated token |
| Secret Access Key | Secret Access Key from the generated token |
| Default Bucket | Bucket to use |
| Path-style addressing | Enabled |

Selecting Cloudflare R2 as the provider fills in the Region and endpoint format automatically.

R2 accepts `us-east-1` and an empty Region as aliases for `auto`. This can be useful with S3-compatible tools that require an AWS-style Region value.

An R2 token restricted to a single bucket may not have permission to call `ListBuckets`. In that case, set **Default Bucket** in the profile. NppS3 will connect directly to that bucket instead of trying to list all buckets first.

## Amazon S3 setup

| Setting | Value |
| --- | --- |
| Provider | Amazon S3 |
| Endpoint | `https://s3.<region>.amazonaws.com` |
| Region | The bucket's actual Region, for example `us-east-1` |
| Path-style addressing | Enable only when required |

Leave **Default Bucket** empty if you want the panel to show every bucket accessible with the current credentials.

## Other S3-compatible storage

Select **Custom S3 Compatible** as the provider and enter the service endpoint, Region, and addressing mode.

Any S3-compatible service that supports SigV4 and `ListObjectsV2` should work. Examples include MinIO, Ceph RGW, and Backblaze B2's S3-compatible endpoint.

For self-hosted S3-compatible servers, path-style addressing is often the most compatible choice.

## Profiles

Open **Plugins → NppS3 → Profiles...** or click the gear button in the panel to manage profiles.

A profile contains:

- Endpoint
- Credentials
- Default Bucket
- Prefix

Default Bucket and Prefix are optional.

Using **Clone** also copies the stored Secret Access Key. This is convenient when creating separate profiles for multiple buckets or prefixes under the same account, since the credentials do not need to be entered again.

**Test Connection** sends an actual signed request from a worker thread. The profile dialog remains responsive while waiting for the network response.

Double-clicking a profile activates it and connects immediately.

## Remote editing

Double-click an object in the tree to download it into the NppS3 cache and open it in Notepad++. From there, edit and save it like a normal local file.

When you press `Ctrl+S`, NppS3:

1. Checks whether the current buffer belongs to a remote object tracked by NppS3. Normal local files are ignored.
2. Calls `HeadObject` to retrieve the current remote ETag and compares it with the ETag recorded when the object was downloaded.
3. If there is no conflict, queues the upload in the background transfer queue.

Upload progress and results are available in the transfer list and activity log.

Cache files are stored under:

```text
%LOCALAPPDATA%\NppS3\Cache\
```

A cache file name combines a hash of the profile, bucket, and object key with a sanitized version of the original file name.

NppS3 does not map S3 object keys directly to Windows paths. Object keys may contain characters that are invalid in Windows file names, are case-sensitive, and can be much longer than `MAX_PATH`.

The hashed cache naming scheme avoids those problems while preserving the original extension, so Notepad++ syntax highlighting continues to work as expected.

## Automatic upload on save

This setting is configured per profile and is enabled by default.

When automatic upload is disabled, pressing `Ctrl+S` saves only the local cache file. The remote object is left unchanged until you upload it manually.

Repeated saves in a short period do not create an ever-growing queue of uploads. If another save occurs while an upload is already in progress, NppS3 schedules one more upload after the current one finishes.

The final upload therefore reflects the most recently saved contents.

## Large uploads and resume support

Files of 64 MiB or larger are uploaded using S3 multipart upload instead of a single `PutObject`.

A single HTTP request body cannot exceed 4 GiB in this implementation, so multipart upload avoids that limit. Objects larger than 4 GiB can be uploaded as long as the storage service itself allows them.

The default part size is 16 MiB. If that would exceed S3's 10,000-part limit, NppS3 automatically increases the part size until the object fits. All parts except the last are kept at exactly the same size, matching Cloudflare R2's multipart requirements.

Each part reads only its own byte range from disk, so large files are never loaded into memory in full.

### Resuming an upload

Resume state is stored in:

```text
%APPDATA%\Notepad++\plugins\config\NppS3\Uploads.xml
```

Each entry records the upload ID, file size and last-modified time, part size, and the parts already confirmed as uploaded.

If an upload is canceled, interrupted by a network error, or resumed after restarting Notepad++, NppS3 calls `ListParts` for the same object. Parts that still match the local resume record are kept, and only the remaining parts are uploaded. Progress also resumes from the number of bytes already stored remotely.

No credentials are stored in this file. Entries are separated using a one-way hash of the endpoint and Access Key ID, preventing resume state from one account from being reused with another.

If the local file has changed since the upload was interrupted, the previous multipart upload is abandoned and the transfer starts over. If the file is unchanged, a failed or canceled upload resumes from where it stopped on the next attempt.

### Incomplete uploads

Incomplete multipart uploads do not appear in `ListObjectsV2`, but their uploaded parts still consume storage.

Right-click a bucket or prefix and choose **Clean Up Incomplete Uploads** to remove them.

NppS3 uses both its local resume records and `ListMultipartUploads`. This is necessary because some S3-compatible services, including MinIO, only return multipart uploads for a full object key rather than for a partial prefix.

## Folder download

Right-click a bucket or prefix, choose **Download Folder**, and select a local destination directory.

NppS3 walks the prefix page by page without a delimiter, downloads every object below it, and recreates the object-key hierarchy as a local directory tree.

Unlike the editing cache, which uses hashed names, folder downloads preserve human-readable object names whenever possible.

Each `/`-separated path segment is sanitized independently. Characters that are not valid in Windows file names are replaced with `_`, and reserved device names receive a prefix. Paths longer than `MAX_PATH` are handled using the `\\?\` form, so deeply nested prefixes are not truncated because of Windows path-length limits.

Zero-byte folder marker keys are created as directories rather than files. Folder downloads use the normal transfer queue, so they can be canceled, and the panel reports the number of successful and failed objects.

## Conflict handling

If the remote object's ETag differs from the value recorded when the object was downloaded, NppS3 offers three choices:

- **Overwrite Remote**  
  Ignore the remote change and upload the current local file.

- **Download Remote**  
  Discard the current local changes and download the remote object again.

- **Cancel**  
  Leave both sides unchanged.

If the remote object has been deleted, NppS3 asks whether to upload the current local file as a new object.

ETag is used only to detect whether an object has changed. NppS3 does not assume that an ETag is the MD5 hash of the file contents. Multipart uploads and server-side encryption can produce ETags that do not match MD5, and Cloudflare R2 does not guarantee such a match either.

## Security

Secret Access Keys are stored in Windows Credential Manager under:

```text
NppS3/<profile-id>
```

The operating system protects these credentials for the current Windows user account.

`NppS3.xml` stores only non-secret configuration such as:

- Endpoint
- Region
- Access Key ID
- Preferences

The Secret Access Key is never written to `NppS3.xml`.

Secret Access Keys, `Authorization` headers, and SigV4 signature values are also excluded from logs and other diagnostic output. This applies to both Release and Debug builds.

The Secret Access Key field in the profile dialog behaves like a normal Windows password field. Existing values are masked and can be replaced when needed.

## Testing

Unit tests cover logic that can be verified without a real bucket or Notepad++ UI.

The main test areas include:

- SigV4 signing against AWS public test vectors
- `ListObjectsV2` XML parsing
- Pagination
- Profile serialization
- Cache path generation for unusual object keys
- Mapping object keys to local folder paths
- Multipart part planning, resume decisions, and resume state files
- Remote-document to local-file mapping
- MIME type detection

Run the unit tests with:

```powershell
.\build\Release\npps3_tests.exe
```

### Local S3 server

The integration tests require a real S3-compatible service. A MinIO container is included so the tests can run locally without a hosted bucket or service charges.

```powershell
docker compose -f tests/docker/docker-compose.yml up -d --wait
. .\tests\docker\minio-env.ps1
.\build\Release\npps3_integration.exe
```

`minio-env.ps1` sets the `NPPS3_TEST_*` environment variables only for the current shell.

The credentials in `docker-compose.yml` are local development values for a service bound only to `127.0.0.1`. They are not used for any external service.

When finished, clean up with:

```powershell
docker compose -f tests/docker/docker-compose.yml down -v
```

### Cloudflare R2 or Amazon S3

Use a `.env` file in the repository root or set test credentials through `NPPS3_TEST_*` environment variables. See `.env.example` for the required values.

```powershell
.\build\Release\npps3_integration.exe .env
```

All objects created by the integration tests are placed under:

```text
npps3-integration-test/<run-id>/
```

After the test run, NppS3 removes objects created under that prefix along with incomplete multipart uploads left by the run. It does not touch objects outside the prefix and never deletes the bucket itself.

If no test credentials are available, the integration runner returns exit code `77` and skips the test.

The test for uploads larger than 4 GiB transfers more than 4 GiB of actual data, so it is disabled by default. Set `NPPS3_TEST_HUGE=1` to include it.

## Troubleshooting

### NppS3 does not appear in the Plugins menu

First, check the installation path:

```text
plugins\NppS3\NppS3.dll
```

Both the directory and DLL must be named `NppS3`.

Also check:

- Whether the DLL architecture matches Notepad++: x64, x86, or ARM64
- Whether Windows has blocked the DLL
- Whether the DLL's Properties dialog shows an **Unblock** option

For a DLL downloaded from the Internet, you may need to right-click the file and choose **Properties → Unblock**.

### `SignatureDoesNotMatch`

This is usually caused by one of two things:

- An incorrect Secret Access Key
- A Region and endpoint combination that does not match

For Cloudflare R2, set Region to `auto`.

The quickest way to verify the configuration is to re-enter the Secret Access Key in the profile dialog and run **Test Connection**.

### `AccessDenied` when connecting

If an R2 API Token is restricted to a single bucket, it may not have `ListBuckets` permission.

Set that bucket as the profile's **Default Bucket**.

### The tree only shows `(empty)`

Either there are no objects under the current Prefix, or the current credentials do not have permission to list them.

If the listing request itself failed, check the activity log for the request error.

## Known limitations

- Drag-and-drop upload is not supported.
- Folder **upload** is not supported. Folder **download** is supported.
- Buckets cannot be created or deleted.
- Object version information is shown read-only in the properties dialog.
- Multipart parts are uploaded sequentially rather than in parallel.
- Prefix-level **Clean Up Incomplete Uploads** requires the storage service to support `ListMultipartUploads` for partial prefixes. AWS S3 and Cloudflare R2 support this. MinIO only responds to full object keys, so on MinIO NppS3 can clean up only the uploads present in its own local resume records.

## Project structure

```text
src/
  plugin/      Notepad++ entry point, lifecycle, notification routing
  ui/          Docking panel, dialogs, icons, string tables
  storage/     SigV4, WinHTTP client, S3 client and XML parsing,
               multipart upload and resume state
  transfer/    Background transfer queue
  documents/   Remote-to-local document mapping
  config/      Profiles and settings
  security/    Credential Manager wrapper
  cache/       Cache/download path generation and cleanup
  util/        Strings, hashes, MIME, logging

tests/
  unit/        doctest suite
  integration/ Real S3/R2/MinIO runner
  hosttest/    CLI for host-level GUI verification
  docker/      MinIO container for local integration tests

.github/
  workflows/   CI build/test workflows and tagged release packaging
```

The storage implementation is isolated behind the `IObjectStorage` interface. This allows the transfer and document layers to be tested without a real network connection.

## License

GPL-3.0-or-later.

Icons are from Mark James [Silk icon set](https://github.com/legacy-icons/famfamfam-silk?tab=License-1-ov-file) and are used under CC-BY 2.5.

tinyxml2 and doctest are included under `third_party/` together with their respective license files.
