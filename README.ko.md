# NppS3

Notepad++에서 Amazon S3와 Cloudflare R2를 직접 다룰 수 있는 플러그인입니다.

오른쪽 도킹 패널에서 버킷과 객체를 탐색하고, 원격 객체를 Notepad++에서 바로 열어 수정할 수 있습니다. 수정한 뒤 `Ctrl+S`를 누르면 변경된 내용이 다시 스토리지에 업로드됩니다.

S3나 R2에 올려 둔 설정 파일, 짧은 스크립트, 정적 사이트 파일 등을 간단히 수정할 때 유용합니다. 동기화나 백업 도구보다는 오브젝트 스토리지를 편집기 안에서 바로 다루기 위한 플러그인에 가깝습니다.

*[English version](README.md)*

---

## 기능

- 오른쪽 **도킹 브라우저 패널**에서 프로필, 객체 트리, 전송 목록, 활동 로그를 확인할 수 있습니다.
- **원격 객체를 바로 편집**할 수 있습니다. 객체를 열어 수정한 뒤 `Ctrl+S`를 누르면 백그라운드에서 업로드합니다.
- **충돌 감지** 기능이 있습니다. 업로드 직전에 원격 객체의 ETag를 다시 확인하고, 파일을 내려받은 뒤 원격 객체가 바뀌었다면 덮어쓸지 여부를 묻습니다.
- `/`를 구분자로 사용하는 `ListObjectsV2` 기반 **프리픽스 탐색**을 사용합니다. 필요한 노드만 불러오며 continuation token으로 다음 페이지를 이어서 가져옵니다.
- 큰 객체는 **멀티파트 업로드**로 전송합니다. 중간에 끊긴 업로드는 **이어받기**가 가능하며 4 GiB 제한에 걸리지 않습니다.
- 버킷이나 프리픽스를 통째로 내려받는 **폴더 다운로드**가 있습니다. 원격 계층을 로컬 폴더 구조로 그대로 재구성합니다.
- 업로드, 다운로드, 다른 이름으로 저장, 삭제, 이름 변경, 이동, 새 파일, 새 폴더, 키 복사, `s3://` URI 복사, 객체 속성 보기를 제공합니다.
- **Secret Access Key는 Windows 자격 증명 관리자에 저장**합니다. 프로필 파일에는 Access Key ID, 엔드포인트 같은 일반 설정만 남습니다.
- UI는 English, 한국어, 日本語, 中文, Русский의 **5개 언어**를 제공합니다. Notepad++의 언어 설정을 따르거나 직접 선택할 수 있습니다.
- AWS SDK나 별도 런타임 DLL이 필요하지 않습니다. 네트워크는 WinHTTP, TLS는 Schannel, 해시는 CNG를 사용하며 배포 파일은 `NppS3.dll` 하나입니다.

## 요구 사항

- x64, x86, ARM64용 Notepad++
  - 플러그인 아키텍처와 Notepad++ 빌드 아키텍처가 같아야 합니다.
  - 8.9.1(x64)에서 테스트했습니다.
  - 사용하는 API 중 가장 오래된 것은 Notepad++ 8.0에 추가된 `NPPM_ADDTOOLBARICON_FORDARKMODE`입니다.
  - 따라서 8.0 이상에서도 동작할 가능성이 높지만, 8.9.1 미만 버전은 별도로 테스트하지 않았습니다.
- Windows 10 또는 Windows 11
- 직접 빌드하는 경우
  - C++ 워크로드가 설치된 Visual Studio 2022 또는 Visual Studio Build Tools
  - CMake 3.21 이상
  - ARM64 빌드는 `MSVC v143 - ARM64 빌드 도구` 구성 요소 필요

## 수동 설치

Notepad++는 DLL과 같은 이름의 폴더에서 플러그인을 찾습니다.

```text
<Notepad++>\plugins\NppS3\NppS3.dll
```

일반 설치본은 보통 다음 경로를 사용합니다.

```text
%PROGRAMFILES%\Notepad++\plugins\NppS3\
```

포터블 버전은 다음과 같습니다.

```text
<포터블 폴더>\plugins\NppS3\
```

`NppS3` 폴더를 만들고 `NppS3.dll`을 넣은 뒤 Notepad++를 다시 시작하면 됩니다.

인터넷에서 내려받은 DLL을 사용하는 경우에는 파일을 오른쪽 클릭해 **속성**을 확인하세요. **차단 해제** 항목이 보이면 체크한 뒤 적용해야 합니다. Windows에서 다운로드한 DLL을 차단해 플러그인이 로드되지 않는 경우가 있습니다.

## 빌드

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

x86은 `-A Win32`, ARM64는 `-A ARM64`로 바꾸면 됩니다. 빌드 디렉터리는 아키텍처별로 따로 두는 편이 좋습니다.

```powershell
cmake -S . -B build-x86   -G "Visual Studio 17 2022" -A Win32
cmake -S . -B build-arm64 -G "Visual Studio 17 2022" -A ARM64
```

빌드 결과물은 다음과 같습니다.

| 파일 | 내용 |
| --- | --- |
| `build\Release\NppS3.dll` | Notepad++ 플러그인 |
| `build\Release\npps3_tests.exe` | 단위 테스트 |
| `build\Release\npps3_integration.exe` | 실제 S3 호환 스토리지를 사용하는 통합 테스트 |
| `build\Release\npps3_s3cli.exe` | 호스트 테스트용 CLI |

CRT는 정적 링크하며 tinyxml2와 doctest는 `third_party/`에 포함되어 있습니다. 별도로 설치할 런타임이나 함께 배포할 DLL은 없습니다.

## Cloudflare R2 설정

Cloudflare 대시보드에서 **R2 → Manage R2 API Tokens**로 이동해 토큰을 만든 뒤 NppS3 프로필에 입력합니다.

| 항목 | 값 |
| --- | --- |
| Provider | Cloudflare R2 |
| Endpoint | `https://<ACCOUNT_ID>.r2.cloudflarestorage.com` |
| Region | `auto` |
| Access Key ID | 생성한 토큰의 Access Key ID |
| Secret Access Key | 생성한 토큰의 Secret Access Key |
| Default Bucket | 사용할 버킷 이름 |
| Path-style addressing | 켬 |

Provider에서 Cloudflare R2를 선택하면 Region과 Endpoint 형식이 자동으로 채워집니다.

R2는 `us-east-1`과 빈 Region 값을 `auto`의 별칭으로 받아들입니다. AWS Region 입력을 요구하는 일부 S3 호환 도구와 함께 사용할 때도 이 방식으로 연결할 수 있습니다.

특정 버킷 하나로 범위를 제한한 R2 토큰에는 `ListBuckets` 권한이 없을 수 있습니다. 이 경우 Default Bucket을 지정하면 버킷 목록을 조회하지 않고 해당 버킷으로 바로 연결합니다.

## Amazon S3 설정

| 항목 | 값 |
| --- | --- |
| Provider | Amazon S3 |
| Endpoint | `https://s3.<region>.amazonaws.com` |
| Region | 버킷의 실제 Region. 예: `us-east-1` |
| Path-style addressing | 필요한 경우에만 켬 |

현재 자격 증명으로 접근할 수 있는 버킷을 모두 패널에 표시하려면 Default Bucket을 비워 두면 됩니다.

## 그 밖의 S3 호환 스토리지

Provider에서 **Custom S3 Compatible**을 선택하고 해당 서비스의 Endpoint, Region, 주소 방식을 입력합니다.

SigV4와 `ListObjectsV2`를 지원하는 S3 호환 서비스라면 사용할 수 있습니다. MinIO, Ceph RGW, Backblaze B2의 S3 호환 엔드포인트 등이 여기에 해당합니다.

직접 운영하는 S3 호환 서버는 대체로 path-style addressing을 사용하는 편이 호환성이 좋습니다.

## 프로필

**플러그인 → NppS3 → 프로필...** 메뉴를 열거나 패널의 톱니바퀴 버튼을 누르면 프로필을 관리할 수 있습니다.

프로필에는 다음 정보가 들어갑니다.

- Endpoint
- 자격 증명
- Default Bucket
- Prefix

Default Bucket과 Prefix는 선택 사항입니다.

**복제**를 사용하면 저장된 Secret Access Key도 함께 복사됩니다. 같은 계정의 여러 버킷이나 Prefix를 프로필로 나눠 사용할 때 자격 증명을 다시 입력하지 않아도 됩니다.

**연결 테스트**는 실제 서명 요청을 워커 스레드에서 보냅니다. 네트워크 응답을 기다리는 동안에도 프로필 창은 멈추지 않습니다.

프로필 목록에서 항목을 두 번 클릭하면 해당 프로필이 활성화되고 바로 연결됩니다.

## 원격 편집

트리에서 객체를 두 번 클릭하면 NppS3 캐시 폴더로 내려받은 뒤 Notepad++에서 엽니다. 이후에는 일반 로컬 파일처럼 수정하고 저장하면 됩니다.

`Ctrl+S`를 누르면 다음 순서로 처리합니다.

1. 현재 버퍼가 NppS3에서 추적 중인 원격 객체인지 확인합니다. 일반 로컬 파일에는 아무 작업도 하지 않습니다.
2. `HeadObject`로 현재 원격 ETag를 가져와 처음 다운로드할 때 기록한 ETag와 비교합니다.
3. 충돌이 없으면 업로드 작업을 백그라운드 전송 큐에 넣습니다.

업로드 진행 상황과 결과는 전송 목록과 활동 로그에서 확인할 수 있습니다.

캐시 파일은 다음 위치에 저장됩니다.

```text
%LOCALAPPDATA%\NppS3\Cache\
```

캐시 파일 이름은 프로필, 버킷, 객체 키의 해시와 정리한 원본 파일 이름을 조합해 만듭니다.

S3 객체 키를 Windows 경로로 그대로 바꾸지 않는 데는 이유가 있습니다. 객체 키에는 Windows 파일 이름에 사용할 수 없는 문자가 들어갈 수 있고, 대소문자를 구분하며, `MAX_PATH`보다 훨씬 길 수도 있습니다.

해시 기반 파일 이름을 사용하면 이런 문제를 피하면서 원래 확장자는 유지할 수 있습니다. 따라서 Notepad++의 문법 강조도 그대로 적용됩니다.

## 저장 시 자동 업로드

프로필별로 설정할 수 있으며 기본값은 켜져 있습니다.

자동 업로드를 끄면 `Ctrl+S`를 눌러도 로컬 캐시만 저장되고 원격 객체는 바뀌지 않습니다. 필요할 때 직접 업로드하면 됩니다.

짧은 시간에 여러 번 저장하더라도 업로드 작업을 계속 쌓지는 않습니다. 이미 업로드 중인 상태에서 다시 저장하면 현재 작업이 끝난 뒤 한 번 더 업로드합니다.

최종적으로는 마지막에 저장한 내용이 버킷에 반영됩니다.

## 대용량 업로드와 이어받기

64 MiB 이상인 파일은 단일 `PutObject` 대신 S3 멀티파트 업로드로 전송합니다.

HTTP 요청 하나가 실을 수 있는 본문은 4 GiB를 넘지 못하므로 멀티파트 업로드로 이 제한을 피합니다. 서비스가 허용하는 크기라면 그보다 큰 객체도 업로드할 수 있습니다.

기본 파트 크기는 16 MiB입니다. 이 크기로 S3의 최대 파트 수인 10,000개를 넘게 되면 객체 크기에 맞춰 파트 크기를 자동으로 늘립니다. 마지막 파트를 제외한 나머지 파트는 모두 같은 크기로 만들며, 이는 Cloudflare R2의 요구 사항에 맞춘 동작입니다.

각 파트는 필요한 오프셋만 파일에서 직접 읽습니다. 대용량 파일 전체를 메모리에 올려 두지 않습니다.

### 이어받기

진행 상태는 다음 파일에 기록합니다.

```text
%APPDATA%\Notepad++\plugins\config\NppS3\Uploads.xml
```

각 항목에는 upload id, 파일 크기와 마지막 수정 시각, 파트 크기, 업로드가 확인된 파트 목록이 들어갑니다.

업로드를 취소했거나 네트워크 오류가 났거나 Notepad++를 재시작한 뒤 같은 객체를 다시 올리면 `ListParts`로 서버 상태를 확인합니다. 기록과 일치하는 파트는 그대로 두고 나머지만 전송하며, 진행률도 이미 올라간 바이트부터 이어집니다.

이 파일에는 자격 증명이 들어가지 않습니다. 항목은 엔드포인트와 Access Key ID의 단방향 해시로 구분하므로 다른 계정의 업로드 상태가 섞이지 않습니다.

업로드가 중단된 뒤 로컬 파일이 바뀌었다면 기존 업로드는 중단하고 처음부터 다시 시작합니다. 파일이 바뀌지 않았다면 실패하거나 취소된 작업도 다음 시도에서 멈춘 지점부터 이어집니다.

### 미완료 업로드

완료되지 않은 멀티파트 업로드는 `ListObjectsV2`에 나타나지 않지만 스토리지 공간은 차지합니다.

버킷이나 프리픽스를 오른쪽 클릭한 뒤 **미완료 업로드 정리**를 선택하면 정리할 수 있습니다.

NppS3는 자체 기록과 `ListMultipartUploads` 결과를 함께 사용합니다. MinIO를 비롯한 일부 서비스는 부분 프리픽스가 아닌 전체 객체 키에 대해서만 `ListMultipartUploads` 결과를 반환하기 때문입니다.

## 폴더 다운로드

버킷이나 프리픽스를 오른쪽 클릭해 **폴더 다운로드**를 선택한 뒤 저장할 로컬 디렉터리를 지정합니다.

NppS3는 구분자 없이 프리픽스를 페이지 단위로 훑어 하위 객체를 모두 내려받고, 객체 키 계층을 로컬 디렉터리 구조로 다시 만듭니다.

편집 캐시는 해시 기반 파일 이름을 쓰지만 폴더 다운로드는 사람이 읽을 수 있는 원래 이름을 최대한 유지합니다.

`/`로 나뉜 각 구간을 따로 정리해 Windows 파일 이름에 사용할 수 없는 문자는 `_`로 바꾸고, 예약된 장치 이름에는 접두어를 붙입니다. `MAX_PATH`를 넘는 경로는 `\\?\` 형식으로 처리하므로 프리픽스가 깊어도 경로 길이 때문에 다운로드가 잘리지 않습니다.

0바이트 폴더 마커 키는 파일 대신 디렉터리로 만듭니다. 다운로드는 일반 전송 큐를 사용하므로 중간에 취소할 수 있으며, 성공한 객체 수와 실패한 객체 수도 패널에서 확인할 수 있습니다.

## 충돌 처리

원격 객체의 ETag가 다운로드 당시 기록한 값과 달라졌다면 다음 세 가지 중 하나를 선택합니다.

- **원격 덮어쓰기**  
  원격 변경 내용을 무시하고 현재 로컬 파일을 업로드합니다.

- **원격 다운로드**  
  현재 로컬 변경 내용을 버리고 원격 객체를 다시 내려받습니다.

- **취소**  
  업로드나 다운로드 없이 현재 상태를 유지합니다.

원격 객체가 이미 삭제됐다면 현재 로컬 파일을 새 객체로 다시 업로드할지 확인합니다.

ETag는 객체 변경 여부를 판단하는 용도로만 사용하며 파일 내용의 MD5 값이라고 가정하지 않습니다. 멀티파트 업로드나 서버 측 암호화를 사용하면 ETag가 MD5와 다를 수 있고, Cloudflare R2도 MD5 일치를 보장하지 않습니다.

## 보안

Secret Access Key는 Windows 자격 증명 관리자에 다음 이름으로 저장됩니다.

```text
NppS3/<프로필-id>
```

자격 증명은 현재 Windows 사용자 계정을 기준으로 운영체제가 보호합니다.

`NppS3.xml`에는 다음과 같은 일반 설정만 기록합니다.

- Endpoint
- Region
- Access Key ID
- 환경 설정

Secret Access Key는 `NppS3.xml`에 저장하지 않습니다.

Secret Access Key, `Authorization` 헤더, SigV4 서명 값은 로그나 다른 진단 출력에도 남기지 않습니다. Release와 Debug 빌드에서 동일하게 적용됩니다.

프로필 대화 상자의 Secret Access Key 입력란은 일반 Windows 비밀번호 입력란처럼 동작합니다. 저장된 값은 마스킹해 표시하고, 필요한 경우 새 값으로 바꿀 수 있습니다.

## 테스트

단위 테스트는 실제 버킷이나 Notepad++ UI 없이 검증할 수 있는 로직을 대상으로 합니다.

주요 테스트 항목은 다음과 같습니다.

- AWS 공개 테스트 벡터를 이용한 SigV4 서명 검증
- `ListObjectsV2` XML 파싱
- 페이지 처리
- 프로필 직렬화
- 특수한 객체 키의 캐시 경로 생성
- 객체 키를 로컬 폴더 경로로 변환하는 매핑
- 멀티파트 파트 계획, 이어받기 판정, 이어받기 기록 파일
- 원격 문서와 로컬 파일 매핑
- MIME 타입 판별

실행 방법:

```powershell
.\build\Release\npps3_tests.exe
```

### 로컬 S3 서버

통합 테스트에는 실제 S3 호환 서비스가 필요합니다. 별도 호스팅 버킷이나 비용 없이 실행할 수 있도록 MinIO 컨테이너를 함께 제공합니다.

```powershell
docker compose -f tests/docker/docker-compose.yml up -d --wait
. .\tests\docker\minio-env.ps1
.\build\Release\npps3_integration.exe
```

`minio-env.ps1`은 현재 셸에만 `NPPS3_TEST_*` 환경 변수를 설정합니다.

`docker-compose.yml`에 들어 있는 자격 증명은 `127.0.0.1`에만 바인딩된 로컬 개발용 값입니다. 외부 서비스에는 사용하지 않습니다.

테스트가 끝나면 다음 명령으로 정리할 수 있습니다.

```powershell
docker compose -f tests/docker/docker-compose.yml down -v
```

### Cloudflare R2 또는 Amazon S3

저장소 루트의 `.env` 파일을 사용하거나 `NPPS3_TEST_*` 환경 변수에 테스트용 자격 증명을 넣습니다. 필요한 항목은 `.env.example`을 참고하세요.

```powershell
.\build\Release\npps3_integration.exe .env
```

테스트 중 만드는 객체는 모두 다음 Prefix 아래에 저장합니다.

```text
npps3-integration-test/<실행-id>/
```

테스트가 끝나면 해당 Prefix 아래에 생성한 객체와 실행 중 남은 미완료 멀티파트 업로드를 정리합니다. 이 Prefix 밖의 객체나 버킷 자체는 건드리지 않습니다.

테스트용 자격 증명이 없으면 종료 코드 `77`을 반환하고 해당 테스트를 건너뜁니다.

4 GiB 초과 업로드 테스트는 실제로 4 GiB가 넘는 데이터를 전송하므로 기본값에서는 실행하지 않습니다. 포함하려면 `NPPS3_TEST_HUGE=1`을 설정하세요.

## 문제 해결

### 플러그인 메뉴에 NppS3가 보이지 않습니다

먼저 설치 경로를 확인하세요.

```text
plugins\NppS3\NppS3.dll
```

폴더 이름과 DLL 이름이 모두 `NppS3`여야 합니다.

다음 항목도 확인하세요.

- DLL 아키텍처가 Notepad++와 일치하는지(x64, x86, ARM64)
- Windows가 DLL을 차단하고 있지 않은지
- DLL 파일 속성에 **차단 해제** 항목이 있는지

인터넷에서 내려받은 DLL이라면 오른쪽 클릭 → **속성 → 차단 해제**를 적용해야 할 수 있습니다.

### `SignatureDoesNotMatch`

대부분 다음 둘 중 하나입니다.

- Secret Access Key가 잘못된 경우
- Region과 Endpoint 조합이 맞지 않는 경우

Cloudflare R2는 Region을 `auto`로 설정하세요.

프로필 대화 상자에서 Secret Access Key를 다시 입력한 뒤 **연결 테스트**를 실행하면 빠르게 확인할 수 있습니다.

### 연결할 때 `AccessDenied`

R2 API Token의 범위를 특정 버킷 하나로 제한했다면 `ListBuckets` 권한이 없을 수 있습니다.

이 경우 프로필의 Default Bucket에 해당 버킷을 지정하세요.

### 트리에 `(비어 있음)`만 표시됩니다

현재 Prefix 아래에 객체가 없거나, 사용 중인 자격 증명에 목록 조회 권한이 없는 경우입니다.

목록 조회 자체가 실패했다면 활동 로그에서 요청 실패 원인을 확인하세요.

## 알려진 한계

- 드래그 앤 드롭 업로드는 지원하지 않습니다.
- 폴더 단위 **업로드**는 지원하지 않습니다. 폴더 **다운로드**는 가능합니다.
- 버킷 생성 및 삭제 기능은 없습니다.
- 객체 버전 정보는 속성 화면에서 읽기 전용으로만 표시합니다.
- 멀티파트의 각 파트는 병렬이 아니라 순차로 업로드합니다.
- 프리픽스 단위 **미완료 업로드 정리**는 해당 서비스가 부분 프리픽스에 대해 `ListMultipartUploads`를 지원해야 합니다. AWS S3와 Cloudflare R2는 지원하지만 MinIO는 전체 객체 키에만 응답하므로, MinIO에서는 NppS3 자체 기록에 남아 있는 업로드만 정리합니다.

## 프로젝트 구조

```text
src/
  plugin/      Notepad++ 진입점, 수명 주기, 알림 라우팅
  ui/          도킹 패널, 대화 상자, 아이콘, 문자열 테이블
  storage/     SigV4, WinHTTP 클라이언트, S3 클라이언트와 XML 파싱,
               멀티파트 업로드와 이어받기 기록
  transfer/    백그라운드 전송 큐
  documents/   원격과 로컬 문서 매핑
  config/      프로필과 설정
  security/    자격 증명 관리자 래퍼
  cache/       캐시·다운로드 경로 생성과 정리
  util/        문자열, 해시, MIME, 로깅

tests/
  unit/        doctest 스위트
  integration/ 실제 S3/R2/MinIO 러너
  hosttest/    호스트 수준 GUI 검증용 CLI
  docker/      로컬 통합 테스트용 MinIO 컨테이너

.github/
  workflows/   CI 빌드·테스트, 태그 릴리스 패키징
```

스토리지 구현은 `IObjectStorage` 인터페이스 뒤로 분리되어 있습니다. 전송 계층과 문서 계층은 실제 네트워크 연결 없이도 테스트할 수 있습니다.

## 라이선스

GPL-3.0-or-later.

아이콘은 Mark James의 [Silk icon set](https://github.com/legacy-icons/famfamfam-silk?tab=License-1-ov-file)을 CC-BY 2.5 조건으로 사용했습니다.

tinyxml2와 doctest는 각 라이선스 파일과 함께 `third_party/`에 포함되어 있습니다.
