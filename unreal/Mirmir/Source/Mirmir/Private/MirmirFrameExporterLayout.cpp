#include "MirmirFrameExporter.h"
#include "MirmirCat.h"
#include "MirmirHabitat.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Math/RotationMatrix.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
bool ReadSmall(const FString& Path, TSharedPtr<FJsonObject>& Object)
{
    const int64 Size = IFileManager::Get().FileSize(*Path);
    FString Text;
    return Size > 0 && Size <= 4096 && FFileHelper::LoadFileToString(Text, *Path) &&
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object) && Object.IsValid();
}

bool Pair(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, FVector2D& Value)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    double X, Y;
    if (!Object->TryGetArrayField(Name, Values) || Values->Num() != 2 ||
        !(*Values)[0].IsValid() || !(*Values)[1].IsValid() ||
        !(*Values)[0]->TryGetNumber(X) || !(*Values)[1]->TryGetNumber(Y) ||
        !FMath::IsFinite(X) || !FMath::IsFinite(Y)) return false;
    Value = FVector2D(X, Y);
    return true;
}

bool Current(const TSharedPtr<FJsonObject>& Object, const FString& Session, bool bPlacement)
{
    double Schema, PID, Created, Expiry;
    FString InputSession;
    const FDateTime Now = FDateTime::UtcNow();
    const double NowMS = double(Now.ToUnixTimestamp()) * 1000.0 + Now.GetMillisecond();
    return Object->TryGetNumberField(TEXT("schema_version"), Schema) && Schema == 1 &&
        Object->TryGetNumberField(TEXT("producer_pid"), PID) && PID == FPlatformProcess::GetCurrentProcessId() &&
        Object->TryGetStringField(TEXT("session_id"), InputSession) && InputSession == Session &&
        Object->TryGetNumberField(bPlacement ? TEXT("created_at_unix_ms") : TEXT("sampled_at_unix_ms"), Created) &&
        FMath::IsFinite(Created) && Object->TryGetNumberField(TEXT("expires_at_unix_ms"), Expiry) && FMath::IsFinite(Expiry) &&
        Created <= NowMS + 100.0 && Expiry >= NowMS && Expiry >= Created && Expiry - Created <= 5000.0;
}
}

void AMirmirFrameExporter::PollDesktopBounds()
{
    AMirmirCat* Cat = Subject.Get();
    TSharedPtr<FJsonObject> Object;
    if (!Cat || !ReadSmall(FPaths::Combine(OutputDirectory, TEXT("viewport.json")), Object) || !Current(Object, SessionId, false)) return;
    FVector2D Minimum, Maximum;
    if (!Pair(Object, TEXT("min_offset_px"), Minimum) || !Pair(Object, TEXT("max_offset_px"), Maximum) ||
        Minimum.X > Maximum.X || Minimum.Y > Maximum.Y ||
        Minimum.GetAbsMax() > 32768.0 || Maximum.GetAbsMax() > 32768.0) return;
    const FRotationMatrix Basis(CameraRotation);
    Cat->SetDesktopBounds(InitialCatLocation, Basis.GetUnitAxis(EAxis::Y), Basis.GetUnitAxis(EAxis::Z),
        FMath::Clamp(DesktopPixelsPerCentimeter, .01f, 20.0f), Minimum, Maximum);
}

void AMirmirFrameExporter::PollPlacement()
{
    PollDesktopBounds();
    AMirmirCat* Cat = Subject.Get();
    if (!Cat || !Cat->Habitat) return;
    const FString Path = FPaths::Combine(OutputDirectory, TEXT("layout-command.json"));
    TSharedPtr<FJsonObject> Object;
    if (!ReadSmall(Path, Object)) return;
    // Transient one-slot UI request. No command or pointer-history archive.
    IFileManager::Get().Delete(*Path, false, true, true);
    FString Request, Item, Command;
    if (!Object->TryGetStringField(TEXT("request_id"), Request) || Request.Len() != 32) return;
    for (TCHAR C : Request)
        if (!((C >= '0' && C <= '9') || (C >= 'a' && C <= 'f'))) return;
    PlacementRequestId = Request;
    PlacementItemId.Reset();
    bPlacementAccepted = false;
    PlacementMessage = TEXT("Yerleşim isteği geçersiz veya süresi doldu.");
    if (!Current(Object, SessionId, true) || !Object->TryGetStringField(TEXT("command"), Command) || Command != TEXT("move_station") ||
        !Object->TryGetStringField(TEXT("item_id"), Item) || (Item != TEXT("bed") && Item != TEXT("bowl"))) return;
    PlacementItemId = Item;
    double Revision;
    if (!Object->TryGetNumberField(TEXT("layout_revision"), Revision) || Revision != double(Cat->Habitat->GetLayoutRevision()))
    { PlacementMessage = TEXT("Yerleşim değişti; konumu yeniden sürükleyin."); return; }
    FVector2D Delta;
    if (!Pair(Object, TEXT("delta_px"), Delta) || Delta.GetAbsMax() > 8192.0) return;
    const FRotationMatrix Basis(CameraRotation);
    const FVector Right = Basis.GetUnitAxis(EAxis::Y), Up = Basis.GetUnitAxis(EAxis::Z);
    const double Determinant = Right.X * Up.Y - Right.Y * Up.X;
    if (FMath::Abs(Determinant) < .05) return;
    const double Scale = FMath::Clamp(DesktopPixelsPerCentimeter, .01f, 20.0f);
    const double R = Delta.X / Scale, U = -Delta.Y / Scale;
    const FVector WorldDelta((R * Up.Y - Right.Y * U) / Determinant,
        (Right.X * U - R * Up.X) / Determinant, 0.0);
    const FName Id(*Item);
    const FVector Candidate = Cat->Habitat->GetStationFloorPosition(Id) + WorldDelta;
    FString Error;
    bPlacementAccepted = Cat->Habitat->SetStationPosition(Id, Candidate, Error);
    PlacementMessage = bPlacementAccepted ? TEXT("Yeni konum kaydedildi.") : Error;
}
