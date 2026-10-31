#include "RealOceanTurnkeyInstaller.h"

#include "RealOcean.h"
#include "RealOceanValidationLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "MaterialEditingLibrary.h"
#include "Factories/MaterialFactoryNew.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionCameraVectorWS.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionSingleLayerWaterMaterialOutput.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTwoSidedSign.h"
#include "Materials/MaterialExpressionWorldPosition.h"

#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "Settings/ProjectPackagingSettings.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"

namespace RealOceanTurnkey
{
    static const TCHAR* GeneratedRoot = TEXT("/Game/RealOceanGenerated");
    static const TCHAR* MPCObjectPath = TEXT("/Game/RealOceanGenerated/MPC_RealOceanGlobal.MPC_RealOceanGlobal");
    static const TCHAR* SurfaceMaterialObjectPath = TEXT("/Game/RealOceanGenerated/M_RealOceanSurface.M_RealOceanSurface");
    static const TCHAR* SurfaceMIObjectPath = TEXT("/Game/RealOceanGenerated/MI_RealOceanSurface.MI_RealOceanSurface");
    static const TCHAR* UnderwaterMaterialObjectPath = TEXT("/Game/RealOceanGenerated/M_PP_RealOceanUnderwater.M_PP_RealOceanUnderwater");
    static const TCHAR* SurfaceSchemaSentinel = TEXT("RealOcean Turnkey v37 Surface Shading");
    static const TCHAR* UnderwaterSchemaSentinel = TEXT("RealOcean Turnkey v37 Underwater Volume");
    static const TCHAR* OwnershipMetadataKey = TEXT("RealOcean.GeneratedOwner");
    static const TCHAR* OwnershipMetadataValue = TEXT("RealOceanTurnkey");
    static const TCHAR* OwnershipKindMetadataKey = TEXT("RealOcean.GeneratedKind");
    static const TCHAR* OwnershipSchemaMetadataKey = TEXT("RealOcean.GeneratedSchema");
    static const TCHAR* OwnershipSchemaMetadataValue = TEXT("1");

    static bool IsRealOceanOwnedAsset(const UObject* Asset, const TCHAR* ExpectedKind)
    {
        if (!Asset || !ExpectedKind)
        {
            return false;
        }
        UPackage* Package = Asset->GetOutermost();
        if (!Package)
        {
            return false;
        }
        FMetaData& MetaData = Package->GetMetaData();
        const FString& Owner = MetaData.GetValue(Asset, OwnershipMetadataKey);
        const FString& Kind = MetaData.GetValue(Asset, OwnershipKindMetadataKey);
        const FString& Schema = MetaData.GetValue(Asset, OwnershipSchemaMetadataKey);
        return Owner.Equals(OwnershipMetadataValue, ESearchCase::CaseSensitive)
            && Kind.Equals(ExpectedKind, ESearchCase::CaseSensitive)
            && Schema.Equals(OwnershipSchemaMetadataValue, ESearchCase::CaseSensitive);
    }

    static void StampRealOceanOwnership(UObject* Asset, const TCHAR* Kind)
    {
        if (!Asset || !Kind)
        {
            return;
        }
        if (UPackage* Package = Asset->GetOutermost())
        {
            FMetaData& MetaData = Package->GetMetaData();
            MetaData.SetValue(Asset, OwnershipMetadataKey, OwnershipMetadataValue);
            MetaData.SetValue(Asset, OwnershipKindMetadataKey, Kind);
            MetaData.SetValue(Asset, OwnershipSchemaMetadataKey, OwnershipSchemaMetadataValue);
            Package->MarkPackageDirty();
        }
    }

    static bool RefuseUnownedCollision(UObject* Asset, const TCHAR* Kind, const TCHAR* ObjectPath)
    {
        if (!Asset || IsRealOceanOwnedAsset(Asset, Kind))
        {
            return false;
        }
        UE_LOG(LogRealOcean, Error, TEXT("RealOcean found an asset at reserved path %s without RealOcean ownership metadata. It will not modify or adopt it automatically. Move/rename the asset, or delete legacy RealOceanGenerated content and let the current installer regenerate it."), ObjectPath);
        return true;
    }

    template<typename TObjectType>
    TObjectType* FindOrCreateAssetDirect(const TCHAR* PackageName, const TCHAR* AssetName, bool& bOutCreated)
    {
        bOutCreated = false;
        const FString ObjectPath = FString::Printf(TEXT("%s.%s"), PackageName, AssetName);
        if (UObject* ExistingObject = LoadObject<UObject>(nullptr, *ObjectPath))
        {
            return Cast<TObjectType>(ExistingObject);
        }

        UPackage* Package = CreatePackage(PackageName);
        TObjectType* Asset = NewObject<TObjectType>(Package, AssetName, RF_Public | RF_Standalone | RF_Transactional);
        if (Asset)
        {
            FAssetRegistryModule::AssetCreated(Asset);
            Package->MarkPackageDirty();
            bOutCreated = true;
        }
        return Asset;
    }

    static UMaterial* CreateMaterialAsset(const TCHAR* AssetName, bool& bOutCreated)
    {
        bOutCreated = false;
        const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), GeneratedRoot, AssetName, AssetName);
        if (UObject* ExistingObject = LoadObject<UObject>(nullptr, *ObjectPath))
        {
            UMaterial* Existing = Cast<UMaterial>(ExistingObject);
            if (!Existing)
            {
                UE_LOG(LogRealOcean, Error, TEXT("RealOcean reserved path %s is occupied by a non-material asset. The installer will not overwrite it."), *ObjectPath);
                return nullptr;
            }
            if (RefuseUnownedCollision(Existing, TEXT("Material"), *ObjectPath))
            {
                return nullptr;
            }
            return Existing;
        }

        IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
        UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
        UMaterial* Created = Cast<UMaterial>(AssetTools.CreateAsset(AssetName, GeneratedRoot, UMaterial::StaticClass(), Factory, FName(TEXT("RealOceanTurnkey")), false));
        bOutCreated = (Created != nullptr);
        if (Created)
        {
            StampRealOceanOwnership(Created, TEXT("Material"));
        }
        return Created;
    }

    static UMaterialInstanceConstant* CreateMaterialInstanceAsset(const TCHAR* AssetName, UMaterialInterface* Parent)
    {
        const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), GeneratedRoot, AssetName, AssetName);
        if (UObject* ExistingObject = LoadObject<UObject>(nullptr, *ObjectPath))
        {
            UMaterialInstanceConstant* Existing = Cast<UMaterialInstanceConstant>(ExistingObject);
            if (!Existing)
            {
                UE_LOG(LogRealOcean, Error, TEXT("RealOcean reserved path %s is occupied by a non-material-instance asset. The installer will not overwrite it."), *ObjectPath);
                return nullptr;
            }
            if (RefuseUnownedCollision(Existing, TEXT("MaterialInstance"), *ObjectPath))
            {
                return nullptr;
            }
            if (Parent && Existing->Parent != Parent)
            {
                UMaterialEditingLibrary::SetMaterialInstanceParent(Existing, Parent);
                UMaterialEditingLibrary::UpdateMaterialInstance(Existing);
                Existing->MarkPackageDirty();
                UE_LOG(LogRealOcean, Warning, TEXT("RealOcean turnkey repaired its owned MI_RealOceanSurface parent drift."));
            }
            return Existing;
        }

        const FString PackageName = FString::Printf(TEXT("%s/%s"), GeneratedRoot, AssetName);
        bool bCreated = false;
        UMaterialInstanceConstant* Instance = FindOrCreateAssetDirect<UMaterialInstanceConstant>(*PackageName, AssetName, bCreated);
        if (!Instance)
        {
            return nullptr;
        }
        if (bCreated)
        {
            StampRealOceanOwnership(Instance, TEXT("MaterialInstance"));
        }
        else if (RefuseUnownedCollision(Instance, TEXT("MaterialInstance"), *ObjectPath))
        {
            return nullptr;
        }
        if (Parent)
        {
            UMaterialEditingLibrary::SetMaterialInstanceParent(Instance, Parent);
            Instance->MarkPackageDirty();
        }
        return Instance;
    }

    static bool IsCanonicalMPCSchema(const UMaterialParameterCollection* MPC)
    {
        if (!MPC)
        {
            return false;
        }

        const TArray<FName> RequiredScalars = URealOceanValidationLibrary::GetRequiredScalarParameters();
        const TArray<FName> RequiredVectors = URealOceanValidationLibrary::GetRequiredVectorParameters();
        if (MPC->ScalarParameters.Num() != RequiredScalars.Num() || MPC->VectorParameters.Num() != RequiredVectors.Num())
        {
            return false;
        }

        TSet<FName> Scalars;
        for (const FCollectionScalarParameter& Parameter : MPC->ScalarParameters)
        {
            if (Scalars.Contains(Parameter.ParameterName) || !RequiredScalars.Contains(Parameter.ParameterName))
            {
                return false;
            }
            Scalars.Add(Parameter.ParameterName);
        }

        TSet<FName> Vectors;
        for (const FCollectionVectorParameter& Parameter : MPC->VectorParameters)
        {
            if (Vectors.Contains(Parameter.ParameterName) || !RequiredVectors.Contains(Parameter.ParameterName) || Scalars.Contains(Parameter.ParameterName))
            {
                return false;
            }
            Vectors.Add(Parameter.ParameterName);
        }

        bool bFoundContract = false;
        const float ContractVersion = MPC->GetScalarParameterDefaultValue(TEXT("RO_ContractVersion"), bFoundContract);
        return bFoundContract && FMath::IsNearlyEqual(ContractVersion, 12.0f);
    }

    static void RebuildCanonicalMPCSchema(UMaterialParameterCollection* MPC)
    {
        MPC->ScalarParameters.Reset();
        MPC->VectorParameters.Reset();

        for (const FName& Name : URealOceanValidationLibrary::GetRequiredScalarParameters())
        {
            FCollectionScalarParameter Parameter;
            Parameter.ParameterName = Name;
            Parameter.Id = FGuid::NewGuid();
            Parameter.DefaultValue = (Name == TEXT("RO_ContractVersion")) ? 12.0f : 0.0f;
            if (Name == TEXT("RO_AirIOR")) Parameter.DefaultValue = 1.0f;
            else if (Name == TEXT("RO_WaterIOR")) Parameter.DefaultValue = 1.333f;
            else if (Name == TEXT("RO_GravityMS2")) Parameter.DefaultValue = 9.80665f;
            else if (Name == TEXT("RO_UnderwaterTransitionMeters")) Parameter.DefaultValue = 0.35f;
            else if (Name == TEXT("RO_SunRelativeIntensity")) Parameter.DefaultValue = 1.0f;
            else if (Name == TEXT("RO_SunCosZenithAir")) Parameter.DefaultValue = 1.0f;
            MPC->ScalarParameters.Add(Parameter);
        }

        for (const FName& Name : URealOceanValidationLibrary::GetRequiredVectorParameters())
        {
            FCollectionVectorParameter Parameter;
            Parameter.ParameterName = Name;
            Parameter.Id = FGuid::NewGuid();
            Parameter.DefaultValue = FLinearColor::Black;
            if (Name == TEXT("RO_SunColor")) Parameter.DefaultValue = FLinearColor::White;
            else if (Name == TEXT("RO_SunDirectionToSourceWS")) Parameter.DefaultValue = FLinearColor(0.0f, 0.0f, 1.0f, 0.0f);
            MPC->VectorParameters.Add(Parameter);
        }

        MPC->StateId = FGuid::NewGuid();
        MPC->PostEditChange();
        MPC->MarkPackageDirty();
    }

    static bool IsGeneratedAssetsCookPathConfigured()
    {
        const UProjectPackagingSettings* Packaging = GetDefault<UProjectPackagingSettings>();
        if (!Packaging)
        {
            return false;
        }
        const FString RequiredPath = TEXT("/Game/RealOceanGenerated");
        return Packaging->DirectoriesToAlwaysCook.ContainsByPredicate(
            [&RequiredPath](const FDirectoryPath& Entry)
            {
                return Entry.Path.Equals(RequiredPath, ESearchCase::IgnoreCase);
            });
    }

    static bool EnsureGeneratedAssetsAreCooked()
    {
        UProjectPackagingSettings* Packaging = GetMutableDefault<UProjectPackagingSettings>();
        if (!Packaging)
        {
            return false;
        }

        const FString RequiredPath = TEXT("/Game/RealOceanGenerated");
        const bool bAlreadyPresent = Packaging->DirectoriesToAlwaysCook.ContainsByPredicate(
            [&RequiredPath](const FDirectoryPath& Entry)
            {
                return Entry.Path.Equals(RequiredPath, ESearchCase::IgnoreCase);
            });

        if (!bAlreadyPresent)
        {
            FDirectoryPath Directory;
            Directory.Path = RequiredPath;
            Packaging->DirectoriesToAlwaysCook.Add(Directory);
            if (!Packaging->TryUpdateDefaultConfigFile(FString(), true))
            {
                UE_LOG(LogRealOcean, Error, TEXT("RealOcean could not persist /Game/RealOceanGenerated in Project Packaging. Check DefaultGame.ini/source-control write access before packaging."));
                return false;
            }
            UE_LOG(LogRealOcean, Display, TEXT("RealOcean added /Game/RealOceanGenerated to Project Packaging > Additional Asset Directories to Cook."));
        }
        return true;
    }

    static UMaterialParameterCollection* EnsureMPC(bool& bOutChanged)
    {
        bOutChanged = false;
        UMaterialParameterCollection* MPC = LoadObject<UMaterialParameterCollection>(nullptr, MPCObjectPath);
        if (!MPC)
        {
            if (UObject* Collision = LoadObject<UObject>(nullptr, MPCObjectPath))
            {
                UE_LOG(LogRealOcean, Error, TEXT("RealOcean reserved MPC path is occupied by %s. The installer will not overwrite it."), *GetNameSafe(Collision));
                return nullptr;
            }
            bool bCreated = false;
            MPC = FindOrCreateAssetDirect<UMaterialParameterCollection>(TEXT("/Game/RealOceanGenerated/MPC_RealOceanGlobal"), TEXT("MPC_RealOceanGlobal"), bCreated);
            if (MPC && bCreated)
            {
                StampRealOceanOwnership(MPC, TEXT("MPC"));
                RebuildCanonicalMPCSchema(MPC);
                bOutChanged = true;
            }
            return MPC;
        }

        if (RefuseUnownedCollision(MPC, TEXT("MPC"), MPCObjectPath))
        {
            return nullptr;
        }

        if (IsCanonicalMPCSchema(MPC))
        {
            return MPC;
        }

        // Ownership metadata is the authority. Once an MPC is explicitly RealOcean-owned, repair any
        // schema drift deterministically; untagged/user assets were already rejected above.
        UE_LOG(LogRealOcean, Warning, TEXT("RealOcean repairing its owned generated MPC schema drift (wrong type, duplicate, stale, or missing parameter)."));
        RebuildCanonicalMPCSchema(MPC);
        bOutChanged = true;
        return MPC;
    }

    template<typename TExpression>
    TExpression* AddExpression(UMaterial* Material, int32 X, int32 Y)
    {
        return Cast<TExpression>(UMaterialEditingLibrary::CreateMaterialExpression(Material, TExpression::StaticClass(), X, Y));
    }

    static UMaterialExpressionComponentMask* AddMask(UMaterial* Material, UMaterialExpression* Source, bool R, bool G, bool B, bool A, int32 X, int32 Y)
    {
        UMaterialExpressionComponentMask* Mask = AddExpression<UMaterialExpressionComponentMask>(Material, X, Y);
        if (!Mask)
        {
            return nullptr;
        }
        Mask->R = R;
        Mask->G = G;
        Mask->B = B;
        Mask->A = A;
        UMaterialEditingLibrary::ConnectMaterialExpressions(Source, TEXT(""), Mask, TEXT("Input"));
        return Mask;
    }

    static UMaterialExpressionCollectionParameter* AddCollectionParam(UMaterial* Material, UMaterialParameterCollection* MPC, FName ParameterName, int32 X, int32 Y)
    {
        UMaterialExpressionCollectionParameter* Expression = AddExpression<UMaterialExpressionCollectionParameter>(Material, X, Y);
        if (!Expression)
        {
            return nullptr;
        }
        Expression->Collection = MPC;
        Expression->ParameterName = ParameterName;
        Expression->ParameterId = MPC ? MPC->GetParameterId(ParameterName) : FGuid();
        Expression->ExpressionGUID = FGuid::NewGuid();
        return Expression;
    }

    static UMaterialExpression* AddMPCValue(UMaterial* Material, UMaterialParameterCollection* MPC, FName ParameterName, int32 Components, int32 X, int32 Y)
    {
        UMaterialExpressionCollectionParameter* Base = AddCollectionParam(Material, MPC, ParameterName, X, Y);
        if (!Base || Components == 4 || Components == 1)
        {
            return Base;
        }
        if (Components == 2)
        {
            return AddMask(Material, Base, true, true, false, false, X + 180, Y);
        }
        return AddMask(Material, Base, true, true, true, false, X + 180, Y);
    }

    static UMaterialExpressionWorldPosition* AddCameraRelativeWorldPosition(UMaterial* Material, int32 X, int32 Y)
    {
        UMaterialExpressionWorldPosition* Position = AddExpression<UMaterialExpressionWorldPosition>(Material, X, Y);
        if (Position)
        {
            Position->WorldPositionShaderOffset = WPT_CameraRelativeNoOffsets;
        }
        return Position;
    }

    static UMaterialExpressionConstant* AddScalarConstant(UMaterial* Material, float Value, int32 X, int32 Y)
    {
        UMaterialExpressionConstant* Expression = AddExpression<UMaterialExpressionConstant>(Material, X, Y);
        if (Expression)
        {
            Expression->R = Value;
        }
        return Expression;
    }

    static UMaterialExpressionConstant3Vector* AddColorConstant(UMaterial* Material, const FLinearColor& Value, int32 X, int32 Y)
    {
        UMaterialExpressionConstant3Vector* Expression = AddExpression<UMaterialExpressionConstant3Vector>(Material, X, Y);
        if (Expression)
        {
            Expression->Constant = Value;
        }
        return Expression;
    }

    static UMaterialExpressionCustom* AddCustom(
        UMaterial* Material,
        const TCHAR* Description,
        const FString& Code,
        ECustomMaterialOutputType OutputType,
        const TArray<FName>& Inputs,
        const TArray<TPair<FName, ECustomMaterialOutputType>>& ExtraOutputs,
        int32 X,
        int32 Y)
    {
        UMaterialExpressionCustom* Custom = AddExpression<UMaterialExpressionCustom>(Material, X, Y);
        if (!Custom)
        {
            return nullptr;
        }
        Custom->Description = Description;
        Custom->Code = Code;
        Custom->OutputType = OutputType;
        for (FName InputName : Inputs)
        {
            FCustomInput& Input = Custom->Inputs.AddDefaulted_GetRef();
            Input.InputName = InputName;
        }
        for (const TPair<FName, ECustomMaterialOutputType>& Extra : ExtraOutputs)
        {
            FCustomOutput& Output = Custom->AdditionalOutputs.AddDefaulted_GetRef();
            Output.OutputName = Extra.Key;
            Output.OutputType = Extra.Value;
        }
        Custom->RebuildOutputs();
        return Custom;
    }

    static void Connect(UMaterialExpression* From, const TCHAR* FromOutput, UMaterialExpression* To, const TCHAR* ToInput)
    {
        if (From && To)
        {
            UMaterialEditingLibrary::ConnectMaterialExpressions(From, FromOutput, To, ToInput);
        }
    }

    static void ConnectMPC(UMaterial* Material, UMaterialParameterCollection* MPC, UMaterialExpressionCustom* Custom, FName InputName, FName ParameterName, int32 Components, int32 X, int32 Y)
    {
        if (UMaterialExpression* Value = AddMPCValue(Material, MPC, ParameterName, Components, X, Y))
        {
            Connect(Value, TEXT(""), Custom, *InputName.ToString());
        }
    }

    static bool HasGeneratedCustomNode(const UMaterial* Material, const FString& Description)
    {
        if (!Material)
        {
            return false;
        }
        for (UMaterialExpression* Expression : UMaterialEditingLibrary::GetMaterialExpressions(Material))
        {
            if (const UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression))
            {
                if (Custom->Description.Equals(Description, ESearchCase::CaseSensitive))
                {
                    return true;
                }
            }
        }
        return false;
    }

    static bool BuildSurfaceMaterial(UMaterial* Material, UMaterialParameterCollection* MPC, bool bForceRebuild)
    {
        if (!Material || !MPC)
        {
            return false;
        }
        if (!IsRealOceanOwnedAsset(Material, TEXT("Material")))
        {
            UE_LOG(LogRealOcean, Error, TEXT("RealOcean refuses to modify /Game/RealOceanGenerated/M_RealOceanSurface because it lacks RealOcean ownership metadata, even if the graph is empty."));
            return false;
        }
        if (!bForceRebuild && IsSurfaceMaterialCurrent(Material))
        {
            return true;
        }
        if (UMaterialEditingLibrary::GetNumMaterialExpressions(Material) > 0)
        {
            UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);
        }

        Material->MaterialDomain = MD_Surface;
        Material->BlendMode = BLEND_Opaque;
        Material->SetShadingModel(MSM_SingleLayerWater);
        Material->TwoSided = true;
        Material->bTangentSpaceNormal = false;
        UMaterialEditingLibrary::SetBaseMaterialUsage(Material, MATUSAGE_Water, true);

        UMaterialExpressionWorldPosition* CameraRelativePosition = AddCameraRelativeWorldPosition(Material, -2400, -200);
        UMaterialExpressionComponentMask* PositionXY = AddMask(Material, CameraRelativePosition, true, true, false, false, -2200, -200);
        UMaterialExpressionCameraPositionWS* CameraPosition = AddExpression<UMaterialExpressionCameraPositionWS>(Material, -2400, -450);
        UMaterialExpressionComponentMask* CameraPositionXY = AddMask(Material, CameraPosition, true, true, false, false, -2200, -450);

        const FString WPOCode = TEXT(R"ROHLSL(
#include "/Plugin/RealOcean/RealOceanWaves.ush"
#if REALOCEAN_SHADER_API_VERSION != 19
#error RealOcean turnkey surface requires shader API 19.
#endif
float4 waveAnchor0, waveAnchor1;
RO_CalculateViewWaveAnchorsM36(CameraWorldXYCm, OceanTimeSeconds, TravelDirectionDegrees, WindDirectionDegrees, SwellSignificantWaveHeightMeters, WindSeaSignificantWaveHeightMeters, DominantSwellPeriodSeconds, WindSeaPeakPeriodSeconds, DirectionalSpread, WindSeaDirectionalSpread, WaveSteepness, WindSeaSteepnessScale, GravityMS2, waveAnchor0, waveAnchor1);
float3 displacementCm;
RO_EvaluateOceanSpectrumDisplacementRelativeQualityAnchored(RelativeToCameraXYCm, TravelDirectionDegrees, WindDirectionDegrees, SwellSignificantWaveHeightMeters, WindSeaSignificantWaveHeightMeters, DominantSwellPeriodSeconds, WindSeaPeakPeriodSeconds, DirectionalSpread, WindSeaDirectionalSpread, WaveSteepness, WindSeaSteepnessScale, GravityMS2, waveAnchor0, waveAnchor1, QualityMode, displacementCm);
return displacementCm;
)ROHLSL");

        const TArray<FName> WPOInputs = {
            TEXT("RelativeToCameraXYCm"), TEXT("CameraWorldXYCm"), TEXT("OceanTimeSeconds"), TEXT("TravelDirectionDegrees"), TEXT("WindDirectionDegrees"),
            TEXT("SwellSignificantWaveHeightMeters"), TEXT("WindSeaSignificantWaveHeightMeters"),
            TEXT("DominantSwellPeriodSeconds"), TEXT("WindSeaPeakPeriodSeconds"), TEXT("DirectionalSpread"),
            TEXT("WindSeaDirectionalSpread"), TEXT("WaveSteepness"), TEXT("WindSeaSteepnessScale"), TEXT("GravityMS2"),
            TEXT("QualityMode")
        };
        UMaterialExpressionCustom* WPO = AddCustom(Material, TEXT("RealOcean WPO"), WPOCode, CMOT_Float3, WPOInputs, {}, -1200, -900);
        Connect(PositionXY, TEXT(""), WPO, TEXT("RelativeToCameraXYCm"));
        Connect(CameraPositionXY, TEXT(""), WPO, TEXT("CameraWorldXYCm"));
        ConnectMPC(Material, MPC, WPO, TEXT("OceanTimeSeconds"), TEXT("RO_OceanTimeSeconds"), 1, -2100, -1920);

        int32 ParamY = -1800;
        struct FPin { const TCHAR* Pin; const TCHAR* Param; int32 Components; };
        const FPin WPOPins[] = {
            {TEXT("TravelDirectionDegrees"), TEXT("RO_WaveTravelDirectionDegrees"),1}, {TEXT("WindDirectionDegrees"),TEXT("RO_WindDirectionDegrees"),1},
            {TEXT("SwellSignificantWaveHeightMeters"),TEXT("RO_SwellSignificantWaveHeightMeters"),1}, {TEXT("WindSeaSignificantWaveHeightMeters"),TEXT("RO_WindSeaSignificantWaveHeightMeters"),1},
            {TEXT("DominantSwellPeriodSeconds"),TEXT("RO_DominantSwellPeriodSeconds"),1}, {TEXT("WindSeaPeakPeriodSeconds"),TEXT("RO_WindSeaPeakPeriodSeconds"),1},
            {TEXT("DirectionalSpread"),TEXT("RO_DirectionalSpread"),1}, {TEXT("WindSeaDirectionalSpread"),TEXT("RO_WindSeaDirectionalSpread"),1},
            {TEXT("WaveSteepness"),TEXT("RO_WaveSteepness"),1}, {TEXT("WindSeaSteepnessScale"),TEXT("RO_WindSeaSteepnessScale"),1},
            {TEXT("GravityMS2"),TEXT("RO_GravityMS2"),1}, {TEXT("QualityMode"),TEXT("RO_QualityMode"),1}
        };
        for (const FPin& Pin : WPOPins)
        {
            ConnectMPC(Material, MPC, WPO, Pin.Pin, Pin.Param, Pin.Components, -2100, ParamY);
            ParamY += 115;
        }
        UMaterialEditingLibrary::ConnectMaterialProperty(WPO, TEXT(""), MP_WorldPositionOffset);

        const FString FoamUVCode = TEXT(R"ROHLSL(
#include "/Plugin/RealOcean/RealOceanSurface.ush"
#if REALOCEAN_SHADER_API_VERSION != 19
#error RealOcean turnkey surface requires shader API 19.
#endif
const float2 worldXYMeters = (CameraWorldXYCm + RelativeToCameraXYCm) * 0.01;
return RO_FoamClipmapUVWorldM36(worldXYMeters, FoamClipmapCenterWorldXYMeters, FoamClipmapCoverageMeters);
)ROHLSL");
        UMaterialExpressionCustom* FoamUV = AddCustom(Material, TEXT("RealOcean Foam Clipmap UV"), FoamUVCode, CMOT_Float2,
            {TEXT("RelativeToCameraXYCm"), TEXT("CameraWorldXYCm"), TEXT("FoamClipmapCenterWorldXYMeters"), TEXT("FoamClipmapCoverageMeters")}, {}, -1150, 1250);
        Connect(PositionXY, TEXT(""), FoamUV, TEXT("RelativeToCameraXYCm"));
        Connect(CameraPositionXY, TEXT(""), FoamUV, TEXT("CameraWorldXYCm"));
        ConnectMPC(Material, MPC, FoamUV, TEXT("FoamClipmapCenterWorldXYMeters"), TEXT("RO_FoamClipmapCenterWorldXYMeters"), 2, -2100, 1200);
        ConnectMPC(Material, MPC, FoamUV, TEXT("FoamClipmapCoverageMeters"), TEXT("RO_FoamClipmapCoverageMeters"), 1, -2100, 1340);

        UMaterialExpressionTextureSampleParameter2D* FoamTexture = AddExpression<UMaterialExpressionTextureSampleParameter2D>(Material, -750, 1250);
        FoamTexture->ParameterName = TEXT("RO_FoamClipmapTexture");
        FoamTexture->ExpressionGUID = FGuid::NewGuid();
        FoamTexture->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
        Connect(FoamUV, TEXT(""), FoamTexture, TEXT("Coordinates"));

        const FString ShadingCode = TEXT(R"ROHLSL(
#include "/Plugin/RealOcean/RealOceanWaves.ush"
#include "/Plugin/RealOcean/RealOceanSurface.ush"
#if REALOCEAN_SHADER_API_VERSION != 19
#error RealOcean turnkey surface requires shader API 19.
#endif
float4 waveAnchor0, waveAnchor1, microAnchor0;
RO_CalculateViewWaveAnchorsM36(CameraWorldXYCm, OceanTimeSeconds, TravelDirectionDegrees, WindDirectionDegrees, SwellSignificantWaveHeightMeters, WindSeaSignificantWaveHeightMeters, DominantSwellPeriodSeconds, WindSeaPeakPeriodSeconds, DirectionalSpread, WindSeaDirectionalSpread, WaveSteepness, WindSeaSteepnessScale, GravityMS2, waveAnchor0, waveAnchor1);
RO_CalculateViewMicroAnchorsM36(CameraWorldXYCm, OceanTimeSeconds, WindDirectionDegrees, microAnchor0);
float3 displacementCm; float3 macroNormalWS; float horizontalJacobian;
RO_EvaluateOceanSpectrumRelativeQualityAnchored(RelativeToCameraXYCm, TravelDirectionDegrees, WindDirectionDegrees, SwellSignificantWaveHeightMeters, WindSeaSignificantWaveHeightMeters, DominantSwellPeriodSeconds, WindSeaPeakPeriodSeconds, DirectionalSpread, WindSeaDirectionalSpread, WaveSteepness, WindSeaSteepnessScale, GravityMS2, waveAnchor0, waveAnchor1, QualityMode, displacementCm, macroNormalWS, horizontalJacobian);
float2 microSlope; float unresolvedMicroSlopeVariance;
RO_EvaluateMicroSlopeRelativeAnchoredFilteredM32Sun(RelativeToCameraXYCm, WindDirectionDegrees, WindSpeedMps, MicroNormalStrength, MicroDetailFadeStartMeters, MicroDetailFadeEndMeters, microAnchor0, QualityMode, microSlope, unresolvedMicroSlopeVariance);
const float3 combinedNormalWS = RO_CombineMacroAndMicroNormal(macroNormalWS, microSlope);
const float breaker = RO_BreakingWaveMaskFiltered(horizontalJacobian, macroNormalWS, displacementCm.z, SignificantWaveHeightMeters, WindSpeedMps, BreakingStrength);
const float instantaneousWhitecap = RO_WhitecapMaskM25(breaker, macroNormalWS, displacementCm.z, SignificantWaveHeightMeters, WindSpeedMps, WhitecapCoverageScale);
const float analyticWhitecap = RO_WhitecapMaskWithAnalyticHistoryM35(instantaneousWhitecap, RelativeToCameraXYCm, CameraWorldXYCm, TravelDirectionDegrees, WindDirectionDegrees, SwellSignificantWaveHeightMeters, WindSeaSignificantWaveHeightMeters, DominantSwellPeriodSeconds, WindSeaPeakPeriodSeconds, DirectionalSpread, WindSeaDirectionalSpread, WaveSteepness, WindSeaSteepnessScale, GravityMS2, waveAnchor0, waveAnchor1, QualityMode, SignificantWaveHeightMeters, WindSpeedMps, BreakingStrength, WhitecapCoverageScale, FoamMemorySeconds, FoamDecayHalfLifeSeconds, FoamWindDriftFraction, FoamSurfaceCurrentInfluence, SurfaceCurrentVelocityXYMps);
const float whitecap = RO_CombineStatefulFoamM29(analyticWhitecap, StatefulFoamDensity, FoamClipmapValid, FoamClipmapBlend, FoamClipmapUV, FoamClipmapCoverageMeters, FoamClipmapEdgeFadeMeters);
WhitecapMask = whitecap; BreakingMask = breaker;
RoughnessOut = RO_SurfaceRoughnessM32Sun(SurfaceRoughness, WindSpeedMps, whitecap, RelativeToCameraXYCm, MicroDetailFadeStartMeters, MicroDetailFadeEndMeters, HorizonRoughnessBoost, unresolvedMicroSlopeVariance, combinedNormalWS);
MacroNormalOut = macroNormalWS; HorizontalJacobianOut = horizontalJacobian;
return combinedNormalWS;
)ROHLSL");

        const TArray<FName> ShadingInputs = {
            TEXT("RelativeToCameraXYCm"), TEXT("CameraWorldXYCm"), TEXT("OceanTimeSeconds"), TEXT("TravelDirectionDegrees"), TEXT("WindDirectionDegrees"), TEXT("SwellSignificantWaveHeightMeters"),
            TEXT("WindSeaSignificantWaveHeightMeters"), TEXT("DominantSwellPeriodSeconds"), TEXT("WindSeaPeakPeriodSeconds"), TEXT("DirectionalSpread"),
            TEXT("WindSeaDirectionalSpread"), TEXT("WaveSteepness"), TEXT("WindSeaSteepnessScale"), TEXT("GravityMS2"),
            TEXT("QualityMode"), TEXT("SignificantWaveHeightMeters"), TEXT("WindSpeedMps"), TEXT("MicroNormalStrength"),
            TEXT("MicroDetailFadeStartMeters"), TEXT("MicroDetailFadeEndMeters"), TEXT("BreakingStrength"), TEXT("WhitecapCoverageScale"),
            TEXT("FoamMemorySeconds"), TEXT("FoamDecayHalfLifeSeconds"), TEXT("FoamWindDriftFraction"), TEXT("FoamSurfaceCurrentInfluence"),
            TEXT("SurfaceCurrentVelocityXYMps"), TEXT("SurfaceRoughness"), TEXT("HorizonRoughnessBoost"), TEXT("StatefulFoamDensity"), TEXT("FoamClipmapUV"),
            TEXT("FoamClipmapCoverageMeters"), TEXT("FoamClipmapEdgeFadeMeters"), TEXT("FoamClipmapValid"), TEXT("FoamClipmapBlend")
        };
        const TArray<TPair<FName, ECustomMaterialOutputType>> ShadingOutputs = {
            {TEXT("WhitecapMask"), CMOT_Float1}, {TEXT("BreakingMask"), CMOT_Float1}, {TEXT("RoughnessOut"), CMOT_Float1},
            {TEXT("MacroNormalOut"), CMOT_Float3}, {TEXT("HorizontalJacobianOut"), CMOT_Float1}
        };
        UMaterialExpressionCustom* Shading = AddCustom(Material, SurfaceSchemaSentinel, ShadingCode, CMOT_Float3, ShadingInputs, ShadingOutputs, -200, -300);
        Connect(PositionXY, TEXT(""), Shading, TEXT("RelativeToCameraXYCm"));
        Connect(CameraPositionXY, TEXT(""), Shading, TEXT("CameraWorldXYCm"));
        ConnectMPC(Material, MPC, Shading, TEXT("OceanTimeSeconds"), TEXT("RO_OceanTimeSeconds"), 1, -2100, 1500);
        Connect(FoamTexture, TEXT("R"), Shading, TEXT("StatefulFoamDensity"));
        Connect(FoamUV, TEXT(""), Shading, TEXT("FoamClipmapUV"));

        const FPin ShadingPins[] = {
            {TEXT("TravelDirectionDegrees"),TEXT("RO_WaveTravelDirectionDegrees"),1}, {TEXT("WindDirectionDegrees"),TEXT("RO_WindDirectionDegrees"),1},
            {TEXT("SwellSignificantWaveHeightMeters"),TEXT("RO_SwellSignificantWaveHeightMeters"),1}, {TEXT("WindSeaSignificantWaveHeightMeters"),TEXT("RO_WindSeaSignificantWaveHeightMeters"),1},
            {TEXT("DominantSwellPeriodSeconds"),TEXT("RO_DominantSwellPeriodSeconds"),1}, {TEXT("WindSeaPeakPeriodSeconds"),TEXT("RO_WindSeaPeakPeriodSeconds"),1},
            {TEXT("DirectionalSpread"),TEXT("RO_DirectionalSpread"),1}, {TEXT("WindSeaDirectionalSpread"),TEXT("RO_WindSeaDirectionalSpread"),1},
            {TEXT("WaveSteepness"),TEXT("RO_WaveSteepness"),1}, {TEXT("WindSeaSteepnessScale"),TEXT("RO_WindSeaSteepnessScale"),1},
            {TEXT("GravityMS2"),TEXT("RO_GravityMS2"),1}, {TEXT("QualityMode"),TEXT("RO_QualityMode"),1},
            {TEXT("SignificantWaveHeightMeters"),TEXT("RO_SignificantWaveHeightMeters"),1}, {TEXT("WindSpeedMps"),TEXT("RO_WindSpeedMps"),1},
            {TEXT("MicroNormalStrength"),TEXT("RO_MicroNormalStrength"),1}, {TEXT("MicroDetailFadeStartMeters"),TEXT("RO_MicroDetailFadeStartMeters"),1},
            {TEXT("MicroDetailFadeEndMeters"),TEXT("RO_MicroDetailFadeEndMeters"),1},
            {TEXT("BreakingStrength"),TEXT("RO_BreakingStrength"),1}, {TEXT("WhitecapCoverageScale"),TEXT("RO_WhitecapCoverageScale"),1},
            {TEXT("FoamMemorySeconds"),TEXT("RO_FoamMemorySeconds"),1}, {TEXT("FoamDecayHalfLifeSeconds"),TEXT("RO_FoamDecayHalfLifeSeconds"),1},
            {TEXT("FoamWindDriftFraction"),TEXT("RO_FoamWindDriftFraction"),1}, {TEXT("FoamSurfaceCurrentInfluence"),TEXT("RO_FoamSurfaceCurrentInfluence"),1},
            {TEXT("SurfaceCurrentVelocityXYMps"),TEXT("RO_SurfaceCurrentVelocityXYMps"),2}, {TEXT("SurfaceRoughness"),TEXT("RO_SurfaceRoughness"),1},
            {TEXT("HorizonRoughnessBoost"),TEXT("RO_HorizonRoughnessBoost"),1}, {TEXT("FoamClipmapCoverageMeters"),TEXT("RO_FoamClipmapCoverageMeters"),1},
            {TEXT("FoamClipmapEdgeFadeMeters"),TEXT("RO_FoamClipmapEdgeFadeMeters"),1}, {TEXT("FoamClipmapValid"),TEXT("RO_FoamClipmapValid"),1},
            {TEXT("FoamClipmapBlend"),TEXT("RO_FoamClipmapBlend"),1}
        };
        ParamY = -1900;
        for (const FPin& Pin : ShadingPins)
        {
            ConnectMPC(Material, MPC, Shading, Pin.Pin, Pin.Param, Pin.Components, 850, ParamY);
            ParamY += 105;
        }

        UMaterialEditingLibrary::ConnectMaterialProperty(Shading, TEXT(""), MP_Normal);
        UMaterialEditingLibrary::ConnectMaterialProperty(Shading, TEXT("RoughnessOut"), MP_Roughness);

        UMaterialExpressionConstant3Vector* DeepWaterColor = AddColorConstant(Material, FLinearColor(0.006f, 0.025f, 0.045f, 1.0f), 350, 1500);
        UMaterialExpressionConstant3Vector* FoamColor = AddColorConstant(Material, FLinearColor(0.82f, 0.90f, 0.94f, 1.0f), 350, 1650);
        UMaterialExpressionLinearInterpolate* BaseColorLerp = AddExpression<UMaterialExpressionLinearInterpolate>(Material, 650, 1550);
        Connect(DeepWaterColor, TEXT(""), BaseColorLerp, TEXT("A"));
        Connect(FoamColor, TEXT(""), BaseColorLerp, TEXT("B"));
        Connect(Shading, TEXT("WhitecapMask"), BaseColorLerp, TEXT("Alpha"));
        UMaterialEditingLibrary::ConnectMaterialProperty(BaseColorLerp, TEXT(""), MP_BaseColor);
        UMaterialEditingLibrary::ConnectMaterialProperty(AddScalarConstant(Material, 0.055f, 650, 1750), TEXT(""), MP_Opacity);
        UMaterialEditingLibrary::ConnectMaterialProperty(AddScalarConstant(Material, 0.5f, 650, 1860), TEXT(""), MP_Specular);
        UMaterialEditingLibrary::ConnectMaterialProperty(AddScalarConstant(Material, 0.0f, 650, 1970), TEXT(""), MP_Metallic);

        const FString TurbidityOpticsCode = TEXT(R"ROHLSL(
#include "/Plugin/RealOcean/RealOceanOptics.ush"
float3 effectiveAbsorption; float3 effectiveScattering;
RO_ApplyTurbidityToOpticalCoefficientsM35(BaseAbsorptionRGB, BaseScatteringRGB, Turbidity, effectiveAbsorption, effectiveScattering);
EffectiveScatteringOut = effectiveScattering;
return effectiveAbsorption;
)ROHLSL");
        UMaterialExpressionCustom* EffectiveOptics = AddCustom(Material, TEXT("RealOcean Turbidity Optics"), TurbidityOpticsCode, CMOT_Float3,
            {TEXT("BaseAbsorptionRGB"), TEXT("BaseScatteringRGB"), TEXT("Turbidity")},
            {{TEXT("EffectiveScatteringOut"), CMOT_Float3}}, 1130, 1040);
        ConnectMPC(Material, MPC, EffectiveOptics, TEXT("BaseAbsorptionRGB"), TEXT("RO_AbsorptionRGB"), 3, 850, 1000);
        ConnectMPC(Material, MPC, EffectiveOptics, TEXT("BaseScatteringRGB"), TEXT("RO_ScatteringRGB"), 3, 850, 1120);
        ConnectMPC(Material, MPC, EffectiveOptics, TEXT("Turbidity"), TEXT("RO_Turbidity"), 1, 850, 1240);

        UMaterialExpressionSingleLayerWaterMaterialOutput* WaterOutput = AddExpression<UMaterialExpressionSingleLayerWaterMaterialOutput>(Material, 1820, 1250);
        UMaterialExpressionConstant* CentimeterScale = AddScalarConstant(Material, 0.01f, 1360, 1390);
        UMaterialExpressionMultiply* AbsorptionCm = AddExpression<UMaterialExpressionMultiply>(Material, 1580, 1100);
        UMaterialExpressionMultiply* ScatteringCm = AddExpression<UMaterialExpressionMultiply>(Material, 1580, 1230);
        Connect(EffectiveOptics, TEXT(""), AbsorptionCm, TEXT("A")); Connect(CentimeterScale, TEXT(""), AbsorptionCm, TEXT("B"));
        Connect(EffectiveOptics, TEXT("EffectiveScatteringOut"), ScatteringCm, TEXT("A")); Connect(CentimeterScale, TEXT(""), ScatteringCm, TEXT("B"));
        Connect(AbsorptionCm, TEXT(""), WaterOutput, TEXT("AbsorptionCoefficients"));
        Connect(ScatteringCm, TEXT(""), WaterOutput, TEXT("ScatteringCoefficients"));
        Connect(AddMPCValue(Material, MPC, TEXT("RO_ForwardScatteringG"), 1, 1200, 1510), TEXT(""), WaterOutput, TEXT("PhaseG"));
        Connect(AddColorConstant(Material, FLinearColor::White, 1200, 1630), TEXT(""), WaterOutput, TEXT("ColorScaleBehindWater"));

        const FString CameraDepthCode = TEXT(R"ROHLSL(
#include "/Plugin/RealOcean/RealOceanWaves.ush"
#if REALOCEAN_SHADER_API_VERSION != 19
#error RealOcean turnkey per-view depth requires shader API 19.
#endif
return max(RO_ViewCameraDynamicDepthMetersM36(CameraWorldPositionCm, SeaLevelCm, OceanTimeSeconds, TravelDirectionDegrees, WindDirectionDegrees, SwellSignificantWaveHeightMeters, WindSeaSignificantWaveHeightMeters, DominantSwellPeriodSeconds, WindSeaPeakPeriodSeconds, DirectionalSpread, WindSeaDirectionalSpread, WaveSteepness, WindSeaSteepnessScale, GravityMS2, QualityMode), 0.0);
)ROHLSL");
        UMaterialExpressionCustom* PerViewCameraDepth = AddCustom(Material, TEXT("RealOcean Per-View Camera Depth"), CameraDepthCode, CMOT_Float1,
            {TEXT("CameraWorldPositionCm"), TEXT("SeaLevelCm"), TEXT("OceanTimeSeconds"), TEXT("TravelDirectionDegrees"), TEXT("WindDirectionDegrees"), TEXT("SwellSignificantWaveHeightMeters"), TEXT("WindSeaSignificantWaveHeightMeters"), TEXT("DominantSwellPeriodSeconds"), TEXT("WindSeaPeakPeriodSeconds"), TEXT("DirectionalSpread"), TEXT("WindSeaDirectionalSpread"), TEXT("WaveSteepness"), TEXT("WindSeaSteepnessScale"), TEXT("GravityMS2"), TEXT("QualityMode")}, {}, 1050, 220);
        Connect(CameraPosition, TEXT(""), PerViewCameraDepth, TEXT("CameraWorldPositionCm"));
        const FPin PerViewDepthPins[] = {
            {TEXT("SeaLevelCm"),TEXT("RO_SeaLevelCm"),1}, {TEXT("OceanTimeSeconds"),TEXT("RO_OceanTimeSeconds"),1},
            {TEXT("TravelDirectionDegrees"),TEXT("RO_WaveTravelDirectionDegrees"),1}, {TEXT("WindDirectionDegrees"),TEXT("RO_WindDirectionDegrees"),1},
            {TEXT("SwellSignificantWaveHeightMeters"),TEXT("RO_SwellSignificantWaveHeightMeters"),1}, {TEXT("WindSeaSignificantWaveHeightMeters"),TEXT("RO_WindSeaSignificantWaveHeightMeters"),1},
            {TEXT("DominantSwellPeriodSeconds"),TEXT("RO_DominantSwellPeriodSeconds"),1}, {TEXT("WindSeaPeakPeriodSeconds"),TEXT("RO_WindSeaPeakPeriodSeconds"),1},
            {TEXT("DirectionalSpread"),TEXT("RO_DirectionalSpread"),1}, {TEXT("WindSeaDirectionalSpread"),TEXT("RO_WindSeaDirectionalSpread"),1},
            {TEXT("WaveSteepness"),TEXT("RO_WaveSteepness"),1}, {TEXT("WindSeaSteepnessScale"),TEXT("RO_WindSeaSteepnessScale"),1},
            {TEXT("GravityMS2"),TEXT("RO_GravityMS2"),1}, {TEXT("QualityMode"),TEXT("RO_QualityMode"),1}
        };
        int32 DepthY = 200;
        for (const FPin& Pin : PerViewDepthPins) { ConnectMPC(Material, MPC, PerViewCameraDepth, Pin.Pin, Pin.Param, Pin.Components, 850, DepthY); DepthY += 70; }

        const FString UnderwaterSunCode = TEXT(R"ROHLSL(
#include "/Plugin/RealOcean/RealOceanSurface.ush"
#if REALOCEAN_SHADER_API_VERSION != 19
#error RealOcean turnkey underwater sun requires shader API 19.
#endif
float visibility; float interfaceTransmission; float angularErrorDegrees;
const float3 underwaterSunRGB = RO_UnderwaterSunWindowM33UW(SurfaceNormalWS, ViewDirectionWS, SunDirectionToSourceWS, SunColor, SurfaceRoughness, AirIOR, WaterIOR, CameraDepthMeters, AbsorptionRGB, ScatteringRGB, SunRelativeIntensity, visibility, interfaceTransmission, angularErrorDegrees);
VisibilityOut = visibility; InterfaceTransmissionOut = interfaceTransmission; AngularErrorDegreesOut = angularErrorDegrees;
return underwaterSunRGB;
)ROHLSL");
        UMaterialExpressionCustom* UnderwaterSun = AddCustom(Material, TEXT("RealOcean Underwater Sun Window"), UnderwaterSunCode, CMOT_Float3,
            {TEXT("SurfaceNormalWS"), TEXT("ViewDirectionWS"), TEXT("SunDirectionToSourceWS"), TEXT("SunColor"), TEXT("SurfaceRoughness"), TEXT("AirIOR"), TEXT("WaterIOR"), TEXT("CameraDepthMeters"), TEXT("AbsorptionRGB"), TEXT("ScatteringRGB"), TEXT("SunRelativeIntensity")},
            {{TEXT("VisibilityOut"), CMOT_Float1}, {TEXT("InterfaceTransmissionOut"), CMOT_Float1}, {TEXT("AngularErrorDegreesOut"), CMOT_Float1}}, 1750, -300);
        Connect(Shading, TEXT(""), UnderwaterSun, TEXT("SurfaceNormalWS"));
        Connect(AddExpression<UMaterialExpressionCameraVectorWS>(Material, 1300, -450), TEXT(""), UnderwaterSun, TEXT("ViewDirectionWS"));
        ConnectMPC(Material, MPC, UnderwaterSun, TEXT("SunDirectionToSourceWS"), TEXT("RO_SunDirectionToSourceWS"), 3, 1300, -300);
        ConnectMPC(Material, MPC, UnderwaterSun, TEXT("SunColor"), TEXT("RO_SunColor"), 3, 1300, -160);
        Connect(Shading, TEXT("RoughnessOut"), UnderwaterSun, TEXT("SurfaceRoughness"));
        ConnectMPC(Material, MPC, UnderwaterSun, TEXT("AirIOR"), TEXT("RO_AirIOR"), 1, 1300, -20);
        ConnectMPC(Material, MPC, UnderwaterSun, TEXT("WaterIOR"), TEXT("RO_WaterIOR"), 1, 1300, 120);
        Connect(PerViewCameraDepth, TEXT(""), UnderwaterSun, TEXT("CameraDepthMeters"));
        Connect(EffectiveOptics, TEXT(""), UnderwaterSun, TEXT("AbsorptionRGB"));
        Connect(EffectiveOptics, TEXT("EffectiveScatteringOut"), UnderwaterSun, TEXT("ScatteringRGB"));
        ConnectMPC(Material, MPC, UnderwaterSun, TEXT("SunRelativeIntensity"), TEXT("RO_SunRelativeIntensity"), 1, 1300, 680);

        const FString SunGlitterCode = TEXT(R"ROHLSL(
#include "/Plugin/RealOcean/RealOceanSurface.ush"
return RO_SunGlitterAssistM35(SurfaceNormalWS, ViewDirectionWS, SunDirectionToSourceWS, SunColor, SurfaceRoughness, AirIOR, WaterIOR, SunRelativeIntensity, QualityMode);
)ROHLSL");
        UMaterialExpressionCustom* SunGlitter = AddCustom(Material, TEXT("RealOcean Cinematic Sun Glitter Assist"), SunGlitterCode, CMOT_Float3,
            {TEXT("SurfaceNormalWS"), TEXT("ViewDirectionWS"), TEXT("SunDirectionToSourceWS"), TEXT("SunColor"), TEXT("SurfaceRoughness"), TEXT("AirIOR"), TEXT("WaterIOR"), TEXT("SunRelativeIntensity"), TEXT("QualityMode")}, {}, 1760, 720);
        Connect(Shading, TEXT(""), SunGlitter, TEXT("SurfaceNormalWS"));
        Connect(AddExpression<UMaterialExpressionCameraVectorWS>(Material, 1500, 780), TEXT(""), SunGlitter, TEXT("ViewDirectionWS"));
        ConnectMPC(Material, MPC, SunGlitter, TEXT("SunDirectionToSourceWS"), TEXT("RO_SunDirectionToSourceWS"), 3, 1500, 900);
        ConnectMPC(Material, MPC, SunGlitter, TEXT("SunColor"), TEXT("RO_SunColor"), 3, 1500, 1020);
        Connect(Shading, TEXT("RoughnessOut"), SunGlitter, TEXT("SurfaceRoughness"));
        ConnectMPC(Material, MPC, SunGlitter, TEXT("AirIOR"), TEXT("RO_AirIOR"), 1, 1500, 1140);
        ConnectMPC(Material, MPC, SunGlitter, TEXT("WaterIOR"), TEXT("RO_WaterIOR"), 1, 1500, 1260);
        ConnectMPC(Material, MPC, SunGlitter, TEXT("SunRelativeIntensity"), TEXT("RO_SunRelativeIntensity"), 1, 1500, 1380);
        ConnectMPC(Material, MPC, SunGlitter, TEXT("QualityMode"), TEXT("RO_QualityMode"), 1, 1500, 1500);

        UMaterialExpressionTwoSidedSign* TwoSidedSign = AddExpression<UMaterialExpressionTwoSidedSign>(Material, 2050, 250);
        UMaterialExpressionSaturate* FrontMask = AddExpression<UMaterialExpressionSaturate>(Material, 2250, 250);
        UMaterialExpressionOneMinus* BackMask = AddExpression<UMaterialExpressionOneMinus>(Material, 2450, 250);
        UMaterialExpressionMultiply* UnderwaterEmission = AddExpression<UMaterialExpressionMultiply>(Material, 2650, 0);
        UMaterialExpressionMultiply* GlitterEmission = AddExpression<UMaterialExpressionMultiply>(Material, 2650, 520);
        UMaterialExpressionAdd* SurfaceEmission = AddExpression<UMaterialExpressionAdd>(Material, 2920, 220);
        Connect(TwoSidedSign, TEXT(""), FrontMask, TEXT("Input"));
        Connect(FrontMask, TEXT(""), BackMask, TEXT("Input"));
        Connect(UnderwaterSun, TEXT(""), UnderwaterEmission, TEXT("A"));
        Connect(BackMask, TEXT(""), UnderwaterEmission, TEXT("B"));
        Connect(SunGlitter, TEXT(""), GlitterEmission, TEXT("A"));
        Connect(FrontMask, TEXT(""), GlitterEmission, TEXT("B"));
        Connect(UnderwaterEmission, TEXT(""), SurfaceEmission, TEXT("A"));
        Connect(GlitterEmission, TEXT(""), SurfaceEmission, TEXT("B"));
        UMaterialEditingLibrary::ConnectMaterialProperty(SurfaceEmission, TEXT(""), MP_EmissiveColor);

        Material->PostEditChange();
        Material->MarkPackageDirty();
        return true;
    }

    static bool BuildUnderwaterPostProcessMaterial(UMaterial* Material, UMaterialParameterCollection* MPC, bool bForceRebuild)
    {
        if (!Material || !MPC)
        {
            return false;
        }
        if (!IsRealOceanOwnedAsset(Material, TEXT("Material")))
        {
            UE_LOG(LogRealOcean, Error, TEXT("RealOcean refuses to modify /Game/RealOceanGenerated/M_PP_RealOceanUnderwater because it lacks RealOcean ownership metadata, even if the graph is empty."));
            return false;
        }
        if (!bForceRebuild && IsUnderwaterMaterialCurrent(Material))
        {
            return true;
        }
        if (UMaterialEditingLibrary::GetNumMaterialExpressions(Material) > 0)
        {
            UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);
        }

        Material->MaterialDomain = MD_PostProcess;
        Material->SetShadingModel(MSM_Unlit);
        Material->bIsBlendable = true;
        Material->BlendableLocation = BL_SceneColorBeforeDOF;

        UMaterialExpressionSceneTexture* SceneColor = AddExpression<UMaterialExpressionSceneTexture>(Material, -1900, -400);
        SceneColor->SceneTextureId = PPI_PostProcessInput0;
        UMaterialExpressionWorldPosition* RelativePosition = AddCameraRelativeWorldPosition(Material, -1900, -150);
        UMaterialExpressionCameraPositionWS* PPCameraPosition = AddExpression<UMaterialExpressionCameraPositionWS>(Material, -1900, 80);
        UMaterialExpressionComponentMask* PPCameraPositionXY = AddMask(Material, PPCameraPosition, true, true, false, false, -1700, 80);

        const FString PPDepthCode = TEXT(R"ROHLSL(
#include "/Plugin/RealOcean/RealOceanWaves.ush"
#if REALOCEAN_SHADER_API_VERSION != 19
#error RealOcean turnkey per-view underwater depth requires shader API 19.
#endif
return max(RO_ViewCameraDynamicDepthMetersM36(CameraWorldPositionCm, SeaLevelCm, OceanTimeSeconds, WaveTravelDirectionDegrees, WindDirectionDegrees, SwellSignificantWaveHeightMeters, WindSeaSignificantWaveHeightMeters, DominantSwellPeriodSeconds, WindSeaPeakPeriodSeconds, DirectionalSpread, WindSeaDirectionalSpread, WaveSteepness, WindSeaSteepnessScale, GravityMS2, QualityMode), 0.0);
)ROHLSL");
        UMaterialExpressionCustom* PPDepth = AddCustom(Material, TEXT("RealOcean Per-View Underwater Depth"), PPDepthCode, CMOT_Float1,
            {TEXT("CameraWorldPositionCm"),TEXT("SeaLevelCm"),TEXT("OceanTimeSeconds"),TEXT("WaveTravelDirectionDegrees"),TEXT("WindDirectionDegrees"),TEXT("SwellSignificantWaveHeightMeters"),TEXT("WindSeaSignificantWaveHeightMeters"),TEXT("DominantSwellPeriodSeconds"),TEXT("WindSeaPeakPeriodSeconds"),TEXT("DirectionalSpread"),TEXT("WindSeaDirectionalSpread"),TEXT("WaveSteepness"),TEXT("WindSeaSteepnessScale"),TEXT("GravityMS2"),TEXT("QualityMode")}, {}, -1150, 250);
        Connect(PPCameraPosition, TEXT(""), PPDepth, TEXT("CameraWorldPositionCm"));
        struct FPPPin { const TCHAR* Pin; const TCHAR* Param; int32 Components; };
        const FPPPin PPDepthPins[] = {
            {TEXT("SeaLevelCm"),TEXT("RO_SeaLevelCm"),1},{TEXT("OceanTimeSeconds"),TEXT("RO_OceanTimeSeconds"),1},{TEXT("WaveTravelDirectionDegrees"),TEXT("RO_WaveTravelDirectionDegrees"),1},{TEXT("WindDirectionDegrees"),TEXT("RO_WindDirectionDegrees"),1},
            {TEXT("SwellSignificantWaveHeightMeters"),TEXT("RO_SwellSignificantWaveHeightMeters"),1},{TEXT("WindSeaSignificantWaveHeightMeters"),TEXT("RO_WindSeaSignificantWaveHeightMeters"),1},{TEXT("DominantSwellPeriodSeconds"),TEXT("RO_DominantSwellPeriodSeconds"),1},{TEXT("WindSeaPeakPeriodSeconds"),TEXT("RO_WindSeaPeakPeriodSeconds"),1},
            {TEXT("DirectionalSpread"),TEXT("RO_DirectionalSpread"),1},{TEXT("WindSeaDirectionalSpread"),TEXT("RO_WindSeaDirectionalSpread"),1},{TEXT("WaveSteepness"),TEXT("RO_WaveSteepness"),1},{TEXT("WindSeaSteepnessScale"),TEXT("RO_WindSeaSteepnessScale"),1},{TEXT("GravityMS2"),TEXT("RO_GravityMS2"),1},{TEXT("QualityMode"),TEXT("RO_QualityMode"),1}
        };
        int32 PPDepthY = 200;
        for (const FPPPin& Pin : PPDepthPins) { ConnectMPC(Material, MPC, PPDepth, Pin.Pin, Pin.Param, Pin.Components, -1450, PPDepthY); PPDepthY += 70; }

        const FString CausticsCode = TEXT(R"ROHLSL(
#include "/Plugin/RealOcean/RealOceanCaustics.ush"
#if REALOCEAN_SHADER_API_VERSION != 19
#error RealOcean turnkey caustics requires shader API 19.
#endif
if (CausticsEnabled < 0.5) return 0.0.xxxx;
float3 effectiveAbsorption; float3 effectiveScattering;
RO_ApplyTurbidityToOpticalCoefficientsM35(AbsorptionRGB, ScatteringRGB, Turbidity, effectiveAbsorption, effectiveScattering);
float4 waveAnchor0, waveAnchor1;
RO_CalculateViewWaveAnchorsM36(CameraWorldXYCm, OceanTimeSeconds, WaveTravelDirectionDegrees, WindDirectionDegrees, SwellSignificantWaveHeightMeters, WindSeaSignificantWaveHeightMeters, DominantSwellPeriodSeconds, WindSeaPeakPeriodSeconds, DirectionalSpread, WindSeaDirectionalSpread, WaveSteepness, WindSeaSteepnessScale, GravityMS2, waveAnchor0, waveAnchor1);
const float receiverDepthMeters=max(CameraDepthMeters-RelativeToCameraCm.z*0.01,0.0);
return RO_EvaluateWaveDerivedCaustics(RelativeToCameraCm, receiverDepthMeters, SunDirectionToSourceWS, SunColor, SunRelativeIntensity, effectiveAbsorption, effectiveScattering, SunCosZenithAir, AirIOR, WaterIOR, 0.0, WaveTravelDirectionDegrees, WindDirectionDegrees, SwellSignificantWaveHeightMeters, WindSeaSignificantWaveHeightMeters, DominantSwellPeriodSeconds, WindSeaPeakPeriodSeconds, DirectionalSpread, WindSeaDirectionalSpread, WaveSteepness, WindSeaSteepnessScale, GravityMS2, waveAnchor0, waveAnchor1, EffectiveCausticsMaxDepthMeters, CausticsDerivativeStepMeters, CausticsScreenSpaceStability, CausticsContrast, CausticsFocusClamp, CausticsIntensity, CausticsSolveIterations, CausticsDerivativeScale, QualityMode);
)ROHLSL");
        const TArray<FName> CausticsInputs = {
            TEXT("RelativeToCameraCm"), TEXT("CameraWorldXYCm"), TEXT("OceanTimeSeconds"), TEXT("CameraDepthMeters"), TEXT("SunDirectionToSourceWS"), TEXT("SunColor"), TEXT("SunRelativeIntensity"),
            TEXT("AbsorptionRGB"), TEXT("ScatteringRGB"), TEXT("Turbidity"), TEXT("SunCosZenithAir"), TEXT("AirIOR"), TEXT("WaterIOR"), TEXT("WaveTravelDirectionDegrees"),
            TEXT("WindDirectionDegrees"), TEXT("SwellSignificantWaveHeightMeters"), TEXT("WindSeaSignificantWaveHeightMeters"), TEXT("DominantSwellPeriodSeconds"),
            TEXT("WindSeaPeakPeriodSeconds"), TEXT("DirectionalSpread"), TEXT("WindSeaDirectionalSpread"), TEXT("WaveSteepness"), TEXT("WindSeaSteepnessScale"),
            TEXT("GravityMS2"), TEXT("EffectiveCausticsMaxDepthMeters"), TEXT("CausticsDerivativeStepMeters"),
            TEXT("CausticsScreenSpaceStability"), TEXT("CausticsContrast"), TEXT("CausticsFocusClamp"), TEXT("CausticsIntensity"), TEXT("CausticsSolveIterations"),
            TEXT("CausticsDerivativeScale"), TEXT("QualityMode"), TEXT("CausticsEnabled")
        };
        UMaterialExpressionCustom* Caustics = AddCustom(Material, TEXT("RealOcean Caustics"), CausticsCode, CMOT_Float4, CausticsInputs, {}, -750, -650);
        Connect(RelativePosition, TEXT(""), Caustics, TEXT("RelativeToCameraCm"));
        Connect(PPCameraPositionXY, TEXT(""), Caustics, TEXT("CameraWorldXYCm"));
        ConnectMPC(Material, MPC, Caustics, TEXT("OceanTimeSeconds"), TEXT("RO_OceanTimeSeconds"), 1, -1450, -1800);
        Connect(PPDepth, TEXT(""), Caustics, TEXT("CameraDepthMeters"));

        struct FPin { const TCHAR* Pin; const TCHAR* Param; int32 Components; };
        const FPin CausticsPins[] = {
            {TEXT("SunDirectionToSourceWS"),TEXT("RO_SunDirectionToSourceWS"),3},
            {TEXT("SunColor"),TEXT("RO_SunColor"),3}, {TEXT("SunRelativeIntensity"),TEXT("RO_SunRelativeIntensity"),1}, {TEXT("AbsorptionRGB"),TEXT("RO_AbsorptionRGB"),3},
            {TEXT("ScatteringRGB"),TEXT("RO_ScatteringRGB"),3}, {TEXT("Turbidity"),TEXT("RO_Turbidity"),1}, {TEXT("SunCosZenithAir"),TEXT("RO_SunCosZenithAir"),1}, {TEXT("AirIOR"),TEXT("RO_AirIOR"),1},
            {TEXT("WaterIOR"),TEXT("RO_WaterIOR"),1}, {TEXT("WaveTravelDirectionDegrees"),TEXT("RO_WaveTravelDirectionDegrees"),1}, {TEXT("WindDirectionDegrees"),TEXT("RO_WindDirectionDegrees"),1},
            {TEXT("SwellSignificantWaveHeightMeters"),TEXT("RO_SwellSignificantWaveHeightMeters"),1}, {TEXT("WindSeaSignificantWaveHeightMeters"),TEXT("RO_WindSeaSignificantWaveHeightMeters"),1},
            {TEXT("DominantSwellPeriodSeconds"),TEXT("RO_DominantSwellPeriodSeconds"),1}, {TEXT("WindSeaPeakPeriodSeconds"),TEXT("RO_WindSeaPeakPeriodSeconds"),1},
            {TEXT("DirectionalSpread"),TEXT("RO_DirectionalSpread"),1}, {TEXT("WindSeaDirectionalSpread"),TEXT("RO_WindSeaDirectionalSpread"),1}, {TEXT("WaveSteepness"),TEXT("RO_WaveSteepness"),1},
            {TEXT("WindSeaSteepnessScale"),TEXT("RO_WindSeaSteepnessScale"),1}, {TEXT("GravityMS2"),TEXT("RO_GravityMS2"),1}, {TEXT("EffectiveCausticsMaxDepthMeters"),TEXT("RO_EffectiveCausticsMaxDepthMeters"),1},
            {TEXT("CausticsDerivativeStepMeters"),TEXT("RO_CausticsDerivativeStepMeters"),1}, {TEXT("CausticsScreenSpaceStability"),TEXT("RO_CausticsScreenSpaceStability"),1},
            {TEXT("CausticsContrast"),TEXT("RO_CausticsContrast"),1}, {TEXT("CausticsFocusClamp"),TEXT("RO_CausticsFocusClamp"),1}, {TEXT("CausticsIntensity"),TEXT("RO_CausticsIntensity"),1},
            {TEXT("CausticsSolveIterations"),TEXT("RO_CausticsSolveIterations"),1}, {TEXT("CausticsDerivativeScale"),TEXT("RO_CausticsDerivativeScale"),1}, {TEXT("QualityMode"),TEXT("RO_QualityMode"),1},
            {TEXT("CausticsEnabled"),TEXT("RO_CausticsEnabled"),1}
        };
        int32 Y = -1700;
        for (const FPin& Pin : CausticsPins)
        {
            ConnectMPC(Material, MPC, Caustics, Pin.Pin, Pin.Param, Pin.Components, -1450, Y);
            Y += 90;
        }

        UMaterialExpressionAdd* ReceiverRadiance = AddExpression<UMaterialExpressionAdd>(Material, -350, -450);
        Connect(SceneColor, TEXT(""), ReceiverRadiance, TEXT("A"));
        Connect(Caustics, TEXT(""), ReceiverRadiance, TEXT("B"));

        const FString VolumeCode = TEXT(R"ROHLSL(
#include "/Plugin/RealOcean/RealOceanOptics.ush"
#if REALOCEAN_SHADER_API_VERSION != 19
#error RealOcean turnkey underwater volume requires shader API 19.
#endif
const float3 viewRayFromCameraWS = normalize(RelativeToCameraCm);
const float pathMeters = min(length(RelativeToCameraCm) * 0.01, max(MaxOpticalPathMeters, 0.01));
const float pixelDepthMeters = max(CameraDepthMeters - RelativeToCameraCm.z * 0.01, 0.0);
float3 effectiveAbsorption; float3 effectiveScattering;
RO_ApplyTurbidityToOpticalCoefficientsM35(AbsorptionRGB, ScatteringRGB, Turbidity, effectiveAbsorption, effectiveScattering);
const float3 sunTransmissionRGB = RO_AverageSunTransmissionAlongSegment(effectiveAbsorption, effectiveScattering, CameraDepthMeters, pixelDepthMeters, SunCosZenithAir, AirIOR, WaterIOR, QualityMode);
const float3 processed = RO_ApplyDirectionalSunScatteringRefracted(SceneColor, InscatterColor, SunColor, sunTransmissionRGB, effectiveAbsorption, effectiveScattering, pathMeters, InscatterIntensity, viewRayFromCameraWS, SunDirectionToSourceWS, AirIOR, WaterIOR, ForwardScatteringG, SunRelativeIntensity);
const float underwaterBlend = smoothstep(0.0, max(UnderwaterTransitionMeters, 0.01), max(CameraDepthMeters, 0.0));
return lerp(OriginalSceneColor, processed, underwaterBlend);
)ROHLSL");
        UMaterialExpressionCustom* Volume = AddCustom(Material, UnderwaterSchemaSentinel, VolumeCode, CMOT_Float3,
            {TEXT("SceneColor"), TEXT("OriginalSceneColor"), TEXT("RelativeToCameraCm"), TEXT("CameraDepthMeters"), TEXT("MaxOpticalPathMeters"), TEXT("AbsorptionRGB"), TEXT("ScatteringRGB"), TEXT("Turbidity"),
             TEXT("InscatterColor"), TEXT("InscatterIntensity"), TEXT("SunColor"), TEXT("SunDirectionToSourceWS"), TEXT("SunCosZenithAir"), TEXT("AirIOR"),
             TEXT("WaterIOR"), TEXT("ForwardScatteringG"), TEXT("SunRelativeIntensity"), TEXT("QualityMode"), TEXT("UnderwaterTransitionMeters")}, {}, 250, -350);
        Connect(ReceiverRadiance, TEXT(""), Volume, TEXT("SceneColor"));
        Connect(SceneColor, TEXT(""), Volume, TEXT("OriginalSceneColor"));
        Connect(RelativePosition, TEXT(""), Volume, TEXT("RelativeToCameraCm"));
        Connect(PPDepth, TEXT(""), Volume, TEXT("CameraDepthMeters"));

        const FPin VolumePins[] = {
            {TEXT("MaxOpticalPathMeters"),TEXT("RO_MaxOpticalPathMeters"),1}, {TEXT("AbsorptionRGB"),TEXT("RO_AbsorptionRGB"),3},
            {TEXT("ScatteringRGB"),TEXT("RO_ScatteringRGB"),3}, {TEXT("Turbidity"),TEXT("RO_Turbidity"),1}, {TEXT("InscatterColor"),TEXT("RO_InscatterColor"),3}, {TEXT("InscatterIntensity"),TEXT("RO_InscatterIntensity"),1},
            {TEXT("SunColor"),TEXT("RO_SunColor"),3}, {TEXT("SunDirectionToSourceWS"),TEXT("RO_SunDirectionToSourceWS"),3}, {TEXT("SunCosZenithAir"),TEXT("RO_SunCosZenithAir"),1},
            {TEXT("AirIOR"),TEXT("RO_AirIOR"),1}, {TEXT("WaterIOR"),TEXT("RO_WaterIOR"),1}, {TEXT("ForwardScatteringG"),TEXT("RO_ForwardScatteringG"),1},
            {TEXT("SunRelativeIntensity"),TEXT("RO_SunRelativeIntensity"),1}, {TEXT("QualityMode"),TEXT("RO_QualityMode"),1}, {TEXT("UnderwaterTransitionMeters"),TEXT("RO_UnderwaterTransitionMeters"),1}
        };
        Y = -1500;
        for (const FPin& Pin : VolumePins)
        {
            ConnectMPC(Material, MPC, Volume, Pin.Pin, Pin.Param, Pin.Components, 900, Y);
            Y += 105;
        }
        UMaterialEditingLibrary::ConnectMaterialProperty(Volume, TEXT(""), MP_EmissiveColor);

        Material->PostEditChange();
        Material->MarkPackageDirty();
        return true;
    }
}

bool FRealOceanTurnkeyInstaller::ValidateTurnkeyContent()
{
    UMaterialParameterCollection* MPC = LoadObject<UMaterialParameterCollection>(nullptr, RealOceanTurnkey::MPCObjectPath);
    UMaterial* SurfaceMaterial = LoadObject<UMaterial>(nullptr, RealOceanTurnkey::SurfaceMaterialObjectPath);
    UMaterial* UnderwaterMaterial = LoadObject<UMaterial>(nullptr, RealOceanTurnkey::UnderwaterMaterialObjectPath);
    UMaterialInstanceConstant* SurfaceMI = LoadObject<UMaterialInstanceConstant>(nullptr, RealOceanTurnkey::SurfaceMIObjectPath);

    if (!MPC || !SurfaceMaterial || !UnderwaterMaterial || !SurfaceMI)
    {
        UE_LOG(LogRealOcean, Error, TEXT("RealOcean turnkey content is missing. Open the project in the editor once so /Game/RealOceanGenerated can be generated and saved before commandlet/cook execution."));
        return false;
    }
    if (!RealOceanTurnkey::IsRealOceanOwnedAsset(MPC, TEXT("MPC"))
        || !RealOceanTurnkey::IsRealOceanOwnedAsset(SurfaceMaterial, TEXT("Material"))
        || !RealOceanTurnkey::IsRealOceanOwnedAsset(UnderwaterMaterial, TEXT("Material"))
        || !RealOceanTurnkey::IsRealOceanOwnedAsset(SurfaceMI, TEXT("MaterialInstance")))
    {
        UE_LOG(LogRealOcean, Error, TEXT("RealOcean turnkey content ownership metadata is missing or invalid. Cook is blocked rather than modifying ambiguous assets."));
        return false;
    }
    if (!RealOceanTurnkey::IsCanonicalMPCSchema(MPC)
        || !RealOceanTurnkey::IsSurfaceMaterialCurrent(SurfaceMaterial)
        || !RealOceanTurnkey::IsUnderwaterMaterialCurrent(UnderwaterMaterial)
        || SurfaceMI->Parent != SurfaceMaterial)
    {
        UE_LOG(LogRealOcean, Error, TEXT("RealOcean turnkey content is stale or structurally invalid. Reopen the editor or run RealOcean.RebuildTurnkeyContent before cooking."));
        return false;
    }
    if (!RealOceanTurnkey::IsGeneratedAssetsCookPathConfigured())
    {
        UE_LOG(LogRealOcean, Error, TEXT("RealOcean turnkey cook validation failed because /Game/RealOceanGenerated is not listed in DirectoriesToAlwaysCook. Open the editor once and allow the installer to persist Project Packaging settings before CI/cook."));
        return false;
    }
    return true;
}

bool FRealOceanTurnkeyInstaller::EnsureTurnkeyContent(bool bForceRebuild)
{
    using namespace RealOceanTurnkey;

    if (!EnsureGeneratedAssetsAreCooked())
    {
        return false;
    }

    bool bMPCChanged = false;
    UMaterialParameterCollection* MPC = EnsureMPC(bMPCChanged);
    if (!MPC)
    {
        UE_LOG(LogRealOcean, Error, TEXT("RealOcean turnkey installer could not create MPC_RealOceanGlobal."));
        return false;
    }

    bool bSurfaceCreated = false;
    bool bUnderwaterCreated = false;
    UMaterial* SurfaceMaterial = CreateMaterialAsset(TEXT("M_RealOceanSurface"), bSurfaceCreated);
    UMaterial* UnderwaterMaterial = CreateMaterialAsset(TEXT("M_PP_RealOceanUnderwater"), bUnderwaterCreated);
    if (!SurfaceMaterial || !UnderwaterMaterial)
    {
        UE_LOG(LogRealOcean, Error, TEXT("RealOcean turnkey installer could not create generated materials."));
        return false;
    }

    const bool bForceGraphRebuild = bForceRebuild || bMPCChanged;
    if (!BuildSurfaceMaterial(SurfaceMaterial, MPC, bForceGraphRebuild)
        || !BuildUnderwaterPostProcessMaterial(UnderwaterMaterial, MPC, bForceGraphRebuild))
    {
        return false;
    }

    // UE 5.8 has a reported editor crash path when UMaterialEditingLibrary::RecompileMaterial()
    // is called on a graph that fails compilation. BuildSurface/BuildUnderwater already call
    // PostEditChange(), which schedules the normal material update path. Avoid the unsafe helper
    // during automatic startup; cook/commandlet shader compilation remains the authoritative CI gate.
    if (!IsSurfaceMaterialCurrent(SurfaceMaterial) || !IsUnderwaterMaterialCurrent(UnderwaterMaterial))
    {
        UE_LOG(LogRealOcean, Error, TEXT("RealOcean generated material graph failed structural validation after rebuild."));
        return false;
    }

    UMaterialInstanceConstant* SurfaceMI = CreateMaterialInstanceAsset(TEXT("MI_RealOceanSurface"), SurfaceMaterial);
    if (!SurfaceMI)
    {
        UE_LOG(LogRealOcean, Error, TEXT("RealOcean turnkey installer could not create MI_RealOceanSurface."));
        return false;
    }
    UMaterialEditingLibrary::UpdateMaterialInstance(SurfaceMI);

    TArray<UPackage*> PackagesToSave;
    PackagesToSave.AddUnique(MPC->GetOutermost());
    PackagesToSave.AddUnique(SurfaceMaterial->GetOutermost());
    PackagesToSave.AddUnique(UnderwaterMaterial->GetOutermost());
    if (SurfaceMI)
    {
        PackagesToSave.AddUnique(SurfaceMI->GetOutermost());
    }

    const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true);
    if (bSaved)
    {
        UE_LOG(LogRealOcean, Display, TEXT("RealOcean turnkey content ready: /Game/RealOceanGenerated (MPC, surface material/instance, underwater post-process)."));
    }
    else
    {
        UE_LOG(LogRealOcean, Error, TEXT("RealOcean generated content could not be auto-saved. Turnkey bootstrap is not considered ready; check source-control/read-only package state."));
        return false;
    }
    return true;
}
