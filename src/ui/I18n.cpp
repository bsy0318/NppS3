// NppS3 — Notepad++ plugin for S3-compatible object storage
// SPDX-License-Identifier: GPL-3.0-or-later

#include "I18n.h"

namespace npps3 {
namespace {

constexpr size_t N = static_cast<size_t>(StrId::COUNT_);

// Order must exactly match StrId.
const wchar_t* const kEN[N] = {
    L"Show S3 Panel",
    L"Upload Current File...",
    L"Profiles...",
    L"Settings...",
    L"About NppS3",

    L"S3 Storage",
    L"Connect",
    L"Refresh",
    L"Upload",
    L"Profiles",
    L"Profiles && Settings",

    L"Operation",
    L"Object",
    L"Progress",
    L"Status",
    L"Pending",
    L"Running",
    L"Completed",
    L"Failed",
    L"Cancelled",

    L"Connect",
    L"List",
    L"Download",
    L"Upload",
    L"Delete",
    L"Copy",
    L"Create",
    L"Properties",

    L"Open / Edit",
    L"Download As...",
    L"Rename...",
    L"Copy Key",
    L"Copy S3 URI",
    L"Delete",
    L"Properties",

    L"New File...",
    L"New Folder...",
    L"Upload File Here...",
    L"Refresh",

    L"Cancel",
    L"Clear Finished",

    L"(no profile configured)",
    L"%s (disconnected)",
    L"connecting...",
    L"(empty)",
    L"loading...",
    L"Connect",
    L"Disconnect",

    L"Connected: %s",
    L"Connection failed: %s",
    L"No profile configured. Open Profiles... to add one.",
    L"Not connected. Press Connect first.",
    L"Delete object?\n\n%s",
    L"Delete ALL objects under this prefix?\n\n%s\n\nThis cannot be undone.",
    L"Uploaded: %s",
    L"Downloaded: %s",
    L"Deleted: %s",
    L"Renamed to: %s",
    L"NppS3 Error",
    L"Remote file changed",
    L"The remote object has changed since it was downloaded:\n\n%s\n\nOverwrite the remote version with your local changes?",
    L"The remote object no longer exists:\n\n%s\n\nUpload your local version again?",
    L"Select a profile first.",
    L"No active document.",
    L"Copied to clipboard.",

    L"Overwrite Remote",
    L"Download Remote",
    L"Cancel",

    L"NppS3 Profiles",
    L"Profiles",
    L"Profile Name",
    L"Provider",
    L"Endpoint",
    L"Region",
    L"Access Key ID",
    L"Secret Access Key",
    L"(leave empty to keep the stored secret)",
    L"Default Bucket",
    L"Default Prefix",
    L"Path-style addressing",
    L"Auto Upload On Save",
    L"New",
    L"Delete",
    L"Save",
    L"Test Connection",
    L"Close",
    L"Profile saved.",
    L"Connection successful.",
    L"Connection failed: %s",
    L"Name, Endpoint and Access Key ID are required.",
    L"A Secret Access Key is required for a new profile.",
    L"Delete profile \"%s\"?\nThe stored secret will also be removed.",

    L"NppS3 Settings",
    L"UI Language",
    L"Remove cached files older than (days)",
    L"Files still open as remote documents are never removed.",
    L"Cache location",
    L"Open Cache Folder",
    L"Clear Cache Now",
    L"Removed %d cached file(s).",
    L"Remove all cached files that are not open as remote documents?",
    L"Appearance",
    L"Local Cache",

    L"Upload Current File",
    L"Profile",
    L"Bucket",
    L"Object Key",
    L"Content-Type",
    L"OK",

    L"New File",
    L"New Folder",
    L"Rename",
    L"Name",

    L"Properties",
    L"Size",
    L"Last Modified",
    L"ETag",
    L"Content-Type",
    L"Storage Class",
    L"Version ID",

    L"About NppS3",
    L"NppS3 %s\n\nAmazon S3 / Cloudflare R2 client for Notepad++.\nBrowse buckets, edit remote objects, auto-upload on save.\n\nLicense: GPL-3.0-or-later",
};

const wchar_t* const kKO[N] = {
    L"S3 패널 표시",
    L"현재 파일 업로드...",
    L"프로필...",
    L"설정...",
    L"NppS3 정보",

    L"S3 저장소",
    L"연결",
    L"새로 고침",
    L"업로드",
    L"프로필",
    L"프로필 및 설정",

    L"작업",
    L"개체",
    L"진행률",
    L"상태",
    L"대기 중",
    L"진행 중",
    L"완료",
    L"실패",
    L"취소됨",

    L"연결",
    L"목록",
    L"다운로드",
    L"업로드",
    L"삭제",
    L"복사",
    L"만들기",
    L"속성",

    L"열기 / 편집",
    L"다른 이름으로 다운로드...",
    L"이름 바꾸기...",
    L"키 복사",
    L"S3 URI 복사",
    L"삭제",
    L"속성",

    L"새 파일...",
    L"새 폴더...",
    L"여기에 파일 업로드...",
    L"새로 고침",

    L"취소",
    L"완료 항목 지우기",

    L"(설정된 프로필 없음)",
    L"%s (연결 안 됨)",
    L"연결 중...",
    L"(비어 있음)",
    L"불러오는 중...",
    L"연결",
    L"연결 끊기",

    L"연결됨: %s",
    L"연결 실패: %s",
    L"설정된 프로필이 없습니다. [프로필...]에서 추가하세요.",
    L"연결되어 있지 않습니다. 먼저 연결하세요.",
    L"개체를 삭제할까요?\n\n%s",
    L"이 접두사 아래의 모든 개체를 삭제할까요?\n\n%s\n\n되돌릴 수 없습니다.",
    L"업로드 완료: %s",
    L"다운로드 완료: %s",
    L"삭제됨: %s",
    L"이름 변경됨: %s",
    L"NppS3 오류",
    L"원격 파일 변경됨",
    L"다운로드 이후 원격 개체가 변경되었습니다:\n\n%s\n\n로컬 변경 내용으로 원격 버전을 덮어쓸까요?",
    L"원격 개체가 더 이상 존재하지 않습니다:\n\n%s\n\n로컬 버전을 다시 업로드할까요?",
    L"먼저 프로필을 선택하세요.",
    L"활성 문서가 없습니다.",
    L"클립보드에 복사되었습니다.",

    L"원격 덮어쓰기",
    L"원격 다운로드",
    L"취소",

    L"NppS3 프로필",
    L"프로필 목록",
    L"프로필 이름",
    L"제공자",
    L"엔드포인트",
    L"리전",
    L"Access Key ID",
    L"Secret Access Key",
    L"(비워 두면 저장된 키를 유지합니다)",
    L"기본 버킷",
    L"기본 접두사",
    L"Path-style 주소 지정",
    L"저장 시 자동 업로드",
    L"새로 만들기",
    L"삭제",
    L"저장",
    L"연결 테스트",
    L"닫기",
    L"프로필이 저장되었습니다.",
    L"연결에 성공했습니다.",
    L"연결 실패: %s",
    L"이름, 엔드포인트, Access Key ID는 필수입니다.",
    L"새 프로필에는 Secret Access Key가 필요합니다.",
    L"프로필 \"%s\"을(를) 삭제할까요?\n저장된 비밀 키도 함께 제거됩니다.",

    L"NppS3 설정",
    L"UI 언어",
    L"캐시 파일 보관 기간(일)",
    L"원격 문서로 열려 있는 파일은 삭제되지 않습니다.",
    L"캐시 위치",
    L"캐시 폴더 열기",
    L"지금 캐시 지우기",
    L"캐시 파일 %d개를 삭제했습니다.",
    L"원격 문서로 열려 있지 않은 캐시 파일을 모두 삭제할까요?",
    L"모양",
    L"로컬 캐시",

    L"현재 파일 업로드",
    L"프로필",
    L"버킷",
    L"개체 키",
    L"Content-Type",
    L"확인",

    L"새 파일",
    L"새 폴더",
    L"이름 바꾸기",
    L"이름",

    L"속성",
    L"크기",
    L"수정한 날짜",
    L"ETag",
    L"Content-Type",
    L"스토리지 클래스",
    L"버전 ID",

    L"NppS3 정보",
    L"NppS3 %s\n\nNotepad++용 Amazon S3 / Cloudflare R2 클라이언트.\n버킷 탐색, 원격 개체 편집, 저장 시 자동 업로드를 지원합니다.\n\n라이선스: GPL-3.0-or-later",
};

const wchar_t* const kJA[N] = {
    L"S3 パネルを表示",
    L"現在のファイルをアップロード...",
    L"プロファイル...",
    L"設定...",
    L"NppS3 について",

    L"S3 ストレージ",
    L"接続",
    L"更新",
    L"アップロード",
    L"プロファイル",
    L"プロファイルと設定",

    L"操作",
    L"オブジェクト",
    L"進捗",
    L"状態",
    L"待機中",
    L"実行中",
    L"完了",
    L"失敗",
    L"キャンセル済み",

    L"接続",
    L"一覧",
    L"ダウンロード",
    L"アップロード",
    L"削除",
    L"コピー",
    L"作成",
    L"プロパティ",

    L"開く / 編集",
    L"名前を付けてダウンロード...",
    L"名前の変更...",
    L"キーをコピー",
    L"S3 URI をコピー",
    L"削除",
    L"プロパティ",

    L"新規ファイル...",
    L"新規フォルダー...",
    L"ここにファイルをアップロード...",
    L"更新",

    L"キャンセル",
    L"完了項目をクリア",

    L"(プロファイル未設定)",
    L"%s (未接続)",
    L"接続中...",
    L"(空)",
    L"読み込み中...",
    L"接続",
    L"切断",

    L"接続しました: %s",
    L"接続に失敗しました: %s",
    L"プロファイルがありません。[プロファイル...] から追加してください。",
    L"接続されていません。先に接続してください。",
    L"オブジェクトを削除しますか?\n\n%s",
    L"このプレフィックス配下のすべてのオブジェクトを削除しますか?\n\n%s\n\n元に戻せません。",
    L"アップロード完了: %s",
    L"ダウンロード完了: %s",
    L"削除しました: %s",
    L"名前を変更しました: %s",
    L"NppS3 エラー",
    L"リモートファイルが変更されています",
    L"ダウンロード後にリモートオブジェクトが変更されています:\n\n%s\n\nローカルの変更でリモートを上書きしますか?",
    L"リモートオブジェクトが存在しません:\n\n%s\n\nローカル版を再アップロードしますか?",
    L"先にプロファイルを選択してください。",
    L"アクティブな文書がありません。",
    L"クリップボードにコピーしました。",

    L"リモートを上書き",
    L"リモートをダウンロード",
    L"キャンセル",

    L"NppS3 プロファイル",
    L"プロファイル一覧",
    L"プロファイル名",
    L"プロバイダー",
    L"エンドポイント",
    L"リージョン",
    L"Access Key ID",
    L"Secret Access Key",
    L"(空欄の場合は保存済みのキーを維持)",
    L"既定のバケット",
    L"既定のプレフィックス",
    L"パス形式アドレス",
    L"保存時に自動アップロード",
    L"新規",
    L"削除",
    L"保存",
    L"接続テスト",
    L"閉じる",
    L"プロファイルを保存しました。",
    L"接続に成功しました。",
    L"接続に失敗しました: %s",
    L"名前・エンドポイント・Access Key ID は必須です。",
    L"新規プロファイルには Secret Access Key が必要です。",
    L"プロファイル「%s」を削除しますか?\n保存された秘密キーも削除されます。",

    L"NppS3 設定",
    L"UI 言語",
    L"キャッシュ保持期間(日)",
    L"リモート文書として開いているファイルは削除されません。",
    L"キャッシュの場所",
    L"キャッシュフォルダーを開く",
    L"今すぐキャッシュを削除",
    L"キャッシュファイル %d 個を削除しました。",
    L"リモート文書として開いていないキャッシュファイルをすべて削除しますか?",
    L"外観",
    L"ローカルキャッシュ",

    L"現在のファイルをアップロード",
    L"プロファイル",
    L"バケット",
    L"オブジェクトキー",
    L"Content-Type",
    L"OK",

    L"新規ファイル",
    L"新規フォルダー",
    L"名前の変更",
    L"名前",

    L"プロパティ",
    L"サイズ",
    L"更新日時",
    L"ETag",
    L"Content-Type",
    L"ストレージクラス",
    L"バージョン ID",

    L"NppS3 について",
    L"NppS3 %s\n\nNotepad++ 用 Amazon S3 / Cloudflare R2 クライアント。\nバケットの参照、リモートオブジェクトの編集、保存時の自動アップロードに対応。\n\nライセンス: GPL-3.0-or-later",
};

const wchar_t* const kZH[N] = {
    L"显示 S3 面板",
    L"上传当前文件...",
    L"配置文件...",
    L"设置...",
    L"关于 NppS3",

    L"S3 存储",
    L"连接",
    L"刷新",
    L"上传",
    L"配置文件",
    L"配置文件与设置",

    L"操作",
    L"对象",
    L"进度",
    L"状态",
    L"等待中",
    L"进行中",
    L"已完成",
    L"失败",
    L"已取消",

    L"连接",
    L"列表",
    L"下载",
    L"上传",
    L"删除",
    L"复制",
    L"创建",
    L"属性",

    L"打开 / 编辑",
    L"下载到...",
    L"重命名...",
    L"复制键",
    L"复制 S3 URI",
    L"删除",
    L"属性",

    L"新建文件...",
    L"新建文件夹...",
    L"上传文件到此处...",
    L"刷新",

    L"取消",
    L"清除已完成",

    L"(未配置配置文件)",
    L"%s (未连接)",
    L"正在连接...",
    L"(空)",
    L"正在加载...",
    L"连接",
    L"断开连接",

    L"已连接: %s",
    L"连接失败: %s",
    L"尚未配置任何配置文件。请通过 [配置文件...] 添加。",
    L"尚未连接。请先连接。",
    L"确定删除该对象?\n\n%s",
    L"确定删除该前缀下的所有对象?\n\n%s\n\n此操作无法撤销。",
    L"上传完成: %s",
    L"下载完成: %s",
    L"已删除: %s",
    L"已重命名为: %s",
    L"NppS3 错误",
    L"远程文件已更改",
    L"自下载以来远程对象已被更改:\n\n%s\n\n是否用本地更改覆盖远程版本?",
    L"远程对象已不存在:\n\n%s\n\n是否重新上传本地版本?",
    L"请先选择配置文件。",
    L"没有活动文档。",
    L"已复制到剪贴板。",

    L"覆盖远程",
    L"下载远程",
    L"取消",

    L"NppS3 配置文件",
    L"配置文件列表",
    L"配置名称",
    L"提供商",
    L"终端节点",
    L"区域",
    L"Access Key ID",
    L"Secret Access Key",
    L"(留空则保留已存储的密钥)",
    L"默认存储桶",
    L"默认前缀",
    L"Path-style 寻址",
    L"保存时自动上传",
    L"新建",
    L"删除",
    L"保存",
    L"测试连接",
    L"关闭",
    L"配置文件已保存。",
    L"连接成功。",
    L"连接失败: %s",
    L"名称、终端节点和 Access Key ID 为必填项。",
    L"新配置文件需要 Secret Access Key。",
    L"确定删除配置文件 “%s”?\n已存储的密钥也将一并删除。",

    L"NppS3 设置",
    L"界面语言",
    L"缓存文件保留天数",
    L"作为远程文档打开的文件不会被删除。",
    L"缓存位置",
    L"打开缓存文件夹",
    L"立即清除缓存",
    L"已删除 %d 个缓存文件。",
    L"是否删除所有未作为远程文档打开的缓存文件?",
    L"外观",
    L"本地缓存",

    L"上传当前文件",
    L"配置文件",
    L"存储桶",
    L"对象键",
    L"Content-Type",
    L"确定",

    L"新建文件",
    L"新建文件夹",
    L"重命名",
    L"名称",

    L"属性",
    L"大小",
    L"修改时间",
    L"ETag",
    L"Content-Type",
    L"存储类别",
    L"版本 ID",

    L"关于 NppS3",
    L"NppS3 %s\n\n适用于 Notepad++ 的 Amazon S3 / Cloudflare R2 客户端。\n支持浏览存储桶、编辑远程对象、保存时自动上传。\n\n许可证: GPL-3.0-or-later",
};

const wchar_t* const kRU[N] = {
    L"Показать панель S3",
    L"Загрузить текущий файл...",
    L"Профили...",
    L"Настройки...",
    L"О NppS3",

    L"Хранилище S3",
    L"Подключить",
    L"Обновить",
    L"Загрузить",
    L"Профили",
    L"Профили и настройки",

    L"Операция",
    L"Объект",
    L"Прогресс",
    L"Статус",
    L"В очереди",
    L"Выполняется",
    L"Завершено",
    L"Ошибка",
    L"Отменено",

    L"Подключение",
    L"Список",
    L"Скачивание",
    L"Загрузка",
    L"Удаление",
    L"Копирование",
    L"Создание",
    L"Свойства",

    L"Открыть / Редактировать",
    L"Скачать как...",
    L"Переименовать...",
    L"Копировать ключ",
    L"Копировать S3 URI",
    L"Удалить",
    L"Свойства",

    L"Новый файл...",
    L"Новая папка...",
    L"Загрузить файл сюда...",
    L"Обновить",

    L"Отмена",
    L"Очистить завершённые",

    L"(профиль не настроен)",
    L"%s (не подключено)",
    L"подключение...",
    L"(пусто)",
    L"загрузка...",
    L"Подключить",
    L"Отключить",

    L"Подключено: %s",
    L"Ошибка подключения: %s",
    L"Профили не настроены. Добавьте профиль через «Профили...».",
    L"Нет подключения. Сначала подключитесь.",
    L"Удалить объект?\n\n%s",
    L"Удалить ВСЕ объекты с этим префиксом?\n\n%s\n\nЭто действие необратимо.",
    L"Загружено: %s",
    L"Скачано: %s",
    L"Удалено: %s",
    L"Переименовано в: %s",
    L"Ошибка NppS3",
    L"Удалённый файл изменён",
    L"Удалённый объект был изменён после скачивания:\n\n%s\n\nПерезаписать удалённую версию локальными изменениями?",
    L"Удалённый объект больше не существует:\n\n%s\n\nЗагрузить локальную версию заново?",
    L"Сначала выберите профиль.",
    L"Нет активного документа.",
    L"Скопировано в буфер обмена.",

    L"Перезаписать удалённый",
    L"Скачать удалённый",
    L"Отмена",

    L"Профили NppS3",
    L"Список профилей",
    L"Имя профиля",
    L"Провайдер",
    L"Endpoint",
    L"Регион",
    L"Access Key ID",
    L"Secret Access Key",
    L"(оставьте пустым, чтобы сохранить текущий ключ)",
    L"Бакет по умолчанию",
    L"Префикс по умолчанию",
    L"Path-style адресация",
    L"Автозагрузка при сохранении",
    L"Создать",
    L"Удалить",
    L"Сохранить",
    L"Проверить подключение",
    L"Закрыть",
    L"Профиль сохранён.",
    L"Подключение успешно.",
    L"Ошибка подключения: %s",
    L"Имя, Endpoint и Access Key ID обязательны.",
    L"Для нового профиля требуется Secret Access Key.",
    L"Удалить профиль «%s»?\nСохранённый секретный ключ также будет удалён.",

    L"Настройки NppS3",
    L"Язык интерфейса",
    L"Удалять файлы кэша старше (дней)",
    L"Файлы, открытые как удалённые документы, не удаляются.",
    L"Расположение кэша",
    L"Открыть папку кэша",
    L"Очистить кэш сейчас",
    L"Удалено файлов кэша: %d.",
    L"Удалить все файлы кэша, не открытые как удалённые документы?",
    L"Внешний вид",
    L"Локальный кэш",

    L"Загрузить текущий файл",
    L"Профиль",
    L"Бакет",
    L"Ключ объекта",
    L"Content-Type",
    L"OK",

    L"Новый файл",
    L"Новая папка",
    L"Переименовать",
    L"Имя",

    L"Свойства",
    L"Размер",
    L"Изменён",
    L"ETag",
    L"Content-Type",
    L"Класс хранения",
    L"ID версии",

    L"О NppS3",
    L"NppS3 %s\n\nКлиент Amazon S3 / Cloudflare R2 для Notepad++.\nПросмотр бакетов, редактирование удалённых объектов, автозагрузка при сохранении.\n\nЛицензия: GPL-3.0-or-later",
};

Lang g_lang = Lang::EN;

const wchar_t* const* TableFor(Lang lang)
{
    switch (lang)
    {
    case Lang::KO: return kKO;
    case Lang::JA: return kJA;
    case Lang::ZH: return kZH;
    case Lang::RU: return kRU;
    default: return kEN;
    }
}

} // namespace

void SetLanguage(Lang lang)
{
    g_lang = (lang == Lang::Auto) ? Lang::EN : lang;
}

Lang CurrentLanguage()
{
    return g_lang;
}

Lang DetectLanguage(const std::string& nativeLangFileName)
{
    std::string f;
    f.reserve(nativeLangFileName.size());
    for (char c : nativeLangFileName)
        f.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);

    if (f.find("korean") != std::string::npos)
        return Lang::KO;
    if (f.find("japanese") != std::string::npos)
        return Lang::JA;
    if (f.find("chinese") != std::string::npos || f.find("taiwanese") != std::string::npos)
        return Lang::ZH;
    if (f.find("russian") != std::string::npos)
        return Lang::RU;
    return Lang::EN;
}

const char* LangToString(Lang lang)
{
    switch (lang)
    {
    case Lang::EN: return "en";
    case Lang::KO: return "ko";
    case Lang::JA: return "ja";
    case Lang::ZH: return "zh";
    case Lang::RU: return "ru";
    default: return "auto";
    }
}

Lang LangFromString(const std::string& s)
{
    if (s == "en") return Lang::EN;
    if (s == "ko") return Lang::KO;
    if (s == "ja") return Lang::JA;
    if (s == "zh") return Lang::ZH;
    if (s == "ru") return Lang::RU;
    return Lang::Auto;
}

const wchar_t* T(StrId id)
{
    size_t i = static_cast<size_t>(id);
    if (i >= N)
        return L"?";
    return TableFor(g_lang)[i];
}

} // namespace npps3
