#include "config/CredentialStore.h"

#include <QByteArray>

#ifdef _WIN32
#include <windows.h>
#include <wincred.h>
#endif

namespace raceengineer {
namespace {
constexpr wchar_t credentialTarget[] = L"RaceEngineer/LLMApiKey";
}

QString CredentialStore::readApiKey()
{
#ifdef _WIN32
    PCREDENTIALW credential = nullptr;
    if (CredReadW(credentialTarget, CRED_TYPE_GENERIC, 0, &credential) == FALSE) {
        return {};
    }
    const QByteArray bytes(reinterpret_cast<const char*>(credential->CredentialBlob),
        static_cast<qsizetype>(credential->CredentialBlobSize));
    CredFree(credential);
    return QString::fromUtf8(bytes);
#else
    return {};
#endif
}

bool CredentialStore::writeApiKey(const QString& apiKey)
{
    if (apiKey.isEmpty()) {
        return clearApiKey();
    }
#ifdef _WIN32
    const QByteArray bytes = apiKey.toUtf8();
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<wchar_t*>(credentialTarget);
    credential.CredentialBlobSize = static_cast<DWORD>(bytes.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(bytes.constData()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<wchar_t*>(L"RaceEngineer");
    return CredWriteW(&credential, 0) != FALSE;
#else
    return false;
#endif
}

bool CredentialStore::clearApiKey()
{
#ifdef _WIN32
    if (CredDeleteW(credentialTarget, CRED_TYPE_GENERIC, 0) != FALSE) {
        return true;
    }
    return GetLastError() == ERROR_NOT_FOUND;
#else
    return false;
#endif
}

} // namespace raceengineer
