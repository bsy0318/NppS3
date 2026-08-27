// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ErrorText.h"

#include "I18n.h"
#include "../util/StringUtil.h"

#include <vector>

namespace npps3 {
namespace {

// One row per message so a missing translation cannot shift the table.
struct Phrase
{
    const wchar_t* en;
    const wchar_t* ko;
    const wchar_t* ja;
    const wchar_t* zh;
    const wchar_t* ru;
};

const wchar_t* Pick(const Phrase& p)
{
    switch (CurrentLanguage())
    {
    case Lang::KO: return p.ko;
    case Lang::JA: return p.ja;
    case Lang::ZH: return p.zh;
    case Lang::RU: return p.ru;
    default:       return p.en;
    }
}

struct DetailPhraseRow
{
    ErrorDetail detail;
    Phrase text;
};

// Failures this plugin raises itself; these can be translated in full.
const DetailPhraseRow kDetails[] = {
{ErrorDetail::Timeout, {
    L"The server did not respond in time.",
    L"서버 응답 시간이 초과되었습니다.",
    L"サーバーが時間内に応答しませんでした。",
    L"服务器未在超时前响应。",
    L"Сервер не ответил вовремя."}},
{ErrorDetail::DnsFailed, {
    L"The host name could not be resolved. Check the endpoint address.",
    L"호스트 이름을 찾을 수 없습니다. 엔드포인트 주소를 확인하세요.",
    L"ホスト名を解決できませんでした。エンドポイントを確認してください。",
    L"无法解析主机名, 请检查终端节点地址。",
    L"Не удалось разрешить имя узла. Проверьте адрес эндпоинта."}},
{ErrorDetail::ConnectFailed, {
    L"Could not connect to the server.",
    L"서버에 연결할 수 없습니다.",
    L"サーバーに接続できませんでした。",
    L"无法连接到服务器。",
    L"Не удалось подключиться к серверу."}},
{ErrorDetail::TlsFailed, {
    L"The TLS certificate could not be verified.",
    L"TLS 인증서를 확인하지 못했습니다.",
    L"TLS 証明書を検証できませんでした。",
    L"无法验证 TLS 证书。",
    L"Не удалось проверить сертификат TLS."}},
{ErrorDetail::LocalOpenFailed, {
    L"The local file to upload could not be opened.",
    L"업로드할 로컬 파일을 열 수 없습니다.",
    L"アップロードするローカルファイルを開けませんでした。",
    L"无法打开要上传的本地文件。",
    L"Не удалось открыть локальный файл для отправки."}},
{ErrorDetail::LocalRangeMissing, {
    L"The local file changed while it was being uploaded.",
    L"업로드하는 동안 로컬 파일이 변경되었습니다.",
    L"アップロード中にローカルファイルが変更されました。",
    L"上传过程中本地文件发生了变化。",
    L"Локальный файл изменился во время отправки."}},
{ErrorDetail::LocalReadFailed, {
    L"The local file could not be read during upload.",
    L"업로드 중 로컬 파일을 읽지 못했습니다.",
    L"アップロード中にローカルファイルを読み取れませんでした。",
    L"上传时无法读取本地文件。",
    L"Не удалось прочитать локальный файл при отправке."}},
{ErrorDetail::LocalCreateFailed, {
    L"The local file for the download could not be created.",
    L"다운로드할 로컬 파일을 만들 수 없습니다.",
    L"ダウンロード先のローカルファイルを作成できませんでした。",
    L"无法创建用于下载的本地文件。",
    L"Не удалось создать локальный файл для загрузки."}},
{ErrorDetail::LocalWriteFailed, {
    L"The local file could not be written during download.",
    L"다운로드 중 로컬 파일에 쓰지 못했습니다.",
    L"ダウンロード中にローカルファイルへ書き込めませんでした。",
    L"下载时无法写入本地文件。",
    L"Не удалось записать локальный файл при загрузке."}},
{ErrorDetail::LocalCloseFailed, {
    L"The downloaded file could not be closed.",
    L"내려받은 파일을 닫지 못했습니다.",
    L"ダウンロードしたファイルを閉じられませんでした。",
    L"无法关闭已下载的文件。",
    L"Не удалось закрыть загруженный файл."}},
{ErrorDetail::LocalMoveFailed, {
    L"The downloaded file could not be moved into place.",
    L"내려받은 파일을 대상 위치로 옮기지 못했습니다.",
    L"ダウンロードしたファイルを所定の場所へ移動できませんでした。",
    L"无法将已下载的文件移动到目标位置。",
    L"Не удалось переместить загруженный файл на место."}},
{ErrorDetail::MalformedResponse, {
    L"The server response could not be understood.",
    L"서버 응답을 해석할 수 없습니다.",
    L"サーバーの応答を解釈できませんでした。",
    L"无法解析服务器响应。",
    L"Не удалось разобрать ответ сервера."}},
{ErrorDetail::IncompleteDownload, {
    L"The download ended before the whole object arrived.",
    L"객체를 다 받기 전에 다운로드가 끝났습니다.",
    L"オブジェクト全体を受信する前にダウンロードが終了しました。",
    L"对象尚未接收完整, 下载即已结束。",
    L"Загрузка завершилась до получения всего объекта."}},
{ErrorDetail::MissingPartEtag, {
    L"The server returned no ETag for the uploaded part.",
    L"업로드한 조각의 ETag를 서버가 반환하지 않았습니다.",
    L"アップロードしたパートの ETag がサーバーから返りませんでした。",
    L"服务器未返回已上传分段的 ETag。",
    L"Сервер не вернул ETag для отправленной части."}},
{ErrorDetail::RequestTooLarge, {
    L"The request body exceeds the 4 GiB limit of a single request.",
    L"요청 본문이 단일 요청의 4 GiB 한도를 넘습니다.",
    L"リクエスト本文が 1 リクエストあたり 4 GiB の上限を超えています。",
    L"请求正文超过单个请求 4 GiB 的上限。",
    L"Тело запроса превышает предел в 4 ГиБ для одного запроса."}},
{ErrorDetail::ResponseTooLarge, {
    L"The server response is too large to handle.",
    L"서버 응답이 처리할 수 있는 크기를 넘습니다.",
    L"サーバーの応答が大きすぎて処理できません。",
    L"服务器响应过大, 无法处理。",
    L"Ответ сервера слишком велик для обработки."}},
{ErrorDetail::MultipartEmptyObject, {
    L"An empty object cannot be uploaded as multipart.",
    L"빈 객체는 멀티파트로 업로드할 수 없습니다.",
    L"空のオブジェクトはマルチパートでアップロードできません。",
    L"空对象无法使用分段上传。",
    L"Пустой объект нельзя отправить составной загрузкой."}},
{ErrorDetail::MultipartObjectTooLarge, {
    L"The object is larger than multipart upload can handle.",
    L"멀티파트 업로드로 처리할 수 있는 크기를 넘는 객체입니다.",
    L"マルチパットアップロードで扱える上限を超えたオブジェクトです。",
    L"该对象超出分段上传可处理的大小。",
    L"Объект больше, чем позволяет составная загрузка."}},
{ErrorDetail::NoStoredCredential, {
    L"This profile has no stored credential. Re-enter the Secret Access Key in Profiles.",
    L"이 프로필에 저장된 자격 증명이 없습니다. 프로필에서 Secret Access Key를 다시 입력하세요.",
    L"このプロファイルに資格情報が保存されていません。プロファイルで Secret Access Key を再入力してください。",
    L"该配置未保存凭证。请在配置中重新输入 Secret Access Key。",
    L"Для этого профиля нет сохранённых учётных данных. Введите Secret Access Key заново."}},
};

struct KindPhraseRow
{
    ErrorKind kind;
    Phrase text;
};

// Fallback wording when the failure came from the service rather than from us.
const KindPhraseRow kKinds[] = {
{ErrorKind::Network, {
    L"A network error occurred.",
    L"네트워크 오류가 발생했습니다.",
    L"ネットワークエラーが発生しました。",
    L"发生网络错误。",
    L"Произошла сетевая ошибка."}},
{ErrorKind::Http, {
    L"The server rejected the request.",
    L"서버가 요청을 거부했습니다.",
    L"サーバーがリクエストを拒否しました。",
    L"服务器拒绝了该请求。",
    L"Сервер отклонил запрос."}},
{ErrorKind::AccessDenied, {
    L"Access denied. The credentials lack permission for this operation.",
    L"접근이 거부되었습니다. 이 작업에 대한 권한이 없습니다.",
    L"アクセスが拒否されました。この操作を行う権限がありません。",
    L"访问被拒绝, 当前凭证没有执行该操作的权限。",
    L"Доступ запрещён: у учётных данных нет прав на эту операцию."}},
{ErrorKind::InvalidCredentials, {
    L"The credentials were rejected. Check the Access Key, Secret Key and Region.",
    L"자격 증명이 거부되었습니다. Access Key와 Secret Key, Region을 확인하세요.",
    L"資格情報が拒否されました。Access Key、Secret Key、Region を確認してください。",
    L"凭证被拒绝, 请检查 Access Key、Secret Key 和 Region。",
    L"Учётные данные отклонены. Проверьте Access Key, Secret Key и Region."}},
{ErrorKind::NoSuchBucket, {
    L"The bucket does not exist.",
    L"버킷이 존재하지 않습니다.",
    L"バケットが存在しません。",
    L"存储桶不存在。",
    L"Бакет не существует."}},
{ErrorKind::NoSuchKey, {
    L"The object does not exist.",
    L"객체가 존재하지 않습니다.",
    L"オブジェクトが存在しません。",
    L"对象不存在。",
    L"Объект не существует."}},
{ErrorKind::Conflict, {
    L"The object changed on the server.",
    L"서버에서 객체가 변경되었습니다.",
    L"サーバー上でオブジェクトが変更されました。",
    L"服务器上的对象已发生变化。",
    L"Объект был изменён на сервере."}},
{ErrorKind::Throttled, {
    L"The service is rate limiting requests. Try again shortly.",
    L"요청이 제한되고 있습니다. 잠시 후 다시 시도하세요.",
    L"リクエストが制限されています。しばらくしてから再試行してください。",
    L"服务正在限流, 请稍后重试。",
    L"Сервис ограничивает частоту запросов. Повторите позже."}},
{ErrorKind::Cancelled, {
    L"Cancelled.",
    L"취소되었습니다.",
    L"キャンセルされました。",
    L"已取消。",
    L"Отменено."}},
{ErrorKind::LocalIo, {
    L"A local file error occurred.",
    L"로컬 파일 처리 중 오류가 발생했습니다.",
    L"ローカルファイルの処理でエラーが発生しました。",
    L"处理本地文件时发生错误。",
    L"Произошла ошибка при работе с локальным файлом."}},
{ErrorKind::Internal, {
    L"An internal error occurred.",
    L"내부 오류가 발생했습니다.",
    L"内部エラーが発生しました。",
    L"发生内部错误。",
    L"Произошла внутренняя ошибка."}},
};

const Phrase kUnknown = {
    L"An unknown error occurred.",
    L"알 수 없는 오류가 발생했습니다.",
    L"不明なエラーが発生しました。",
    L"发生未知错误。",
    L"Произошла неизвестная ошибка."};

struct CodePhraseRow
{
    const char* s3Code;
    Phrase text;
};

// Codes whose ErrorKind mapping would mislead: a missing upload id is not a
// missing object, and a too-small part is a multipart rule, not a bad request.
const CodePhraseRow kCodes[] = {
{"NoSuchUpload", {
    L"The interrupted upload is no longer on the server; it will restart from the beginning.",
    L"이어받을 업로드가 서버에 없습니다. 처음부터 다시 업로드합니다.",
    L"再開対象のアップロードがサーバーに存在しません。最初からやり直します。",
    L"服务器上已没有可续传的上传, 将从头重新上传。",
    L"Прерванной отправки больше нет на сервере, она начнётся заново."}},
{"EntityTooSmall", {
    L"A multipart part is smaller than the service allows.",
    L"업로드 조각이 서비스가 허용하는 최소 크기보다 작습니다.",
    L"パートがサービスの許容する最小サイズを下回っています。",
    L"分段小于服务允许的最小尺寸。",
    L"Часть меньше минимального размера, разрешённого сервисом."}},
};

const Phrase* Lookup(const StorageError& e)
{
    if (e.detail != ErrorDetail::None)
    {
        for (const DetailPhraseRow& row : kDetails)
            if (row.detail == e.detail)
                return &row.text;
    }
    if (!e.s3Code.empty())
    {
        for (const CodePhraseRow& row : kCodes)
            if (e.s3Code == row.s3Code)
                return &row.text;
    }
    for (const KindPhraseRow& row : kKinds)
        if (row.kind == e.kind)
            return &row.text;
    return nullptr;
}

} // namespace

std::wstring DescribeErrorLocalized(const StorageError& e)
{
    const Phrase* phrase = Lookup(e);
    std::wstring text = Pick(phrase ? *phrase : kUnknown);

    // An unrecognized service code has no translation; keep the server wording.
    const bool recognized = e.detail != ErrorDetail::None || e.kind != ErrorKind::Http;
    if (!recognized && !e.message.empty() && e.message != "Request failed")
        text += L" " + Utf8ToWide(e.message);

    std::vector<std::wstring> tags;
    if (!e.s3Code.empty())
        tags.push_back(Utf8ToWide(e.s3Code));
    if (e.httpStatus > 0)
        tags.push_back(L"HTTP " + std::to_wstring(e.httpStatus));
    if (e.win32 != 0 && e.s3Code.empty())
        tags.push_back(L"Win32 " + std::to_wstring(e.win32));

    if (!tags.empty())
    {
        text += L" (";
        for (size_t i = 0; i < tags.size(); ++i)
        {
            if (i > 0)
                text += L", ";
            text += tags[i];
        }
        text += L")";
    }
    return text;
}

} // namespace npps3
