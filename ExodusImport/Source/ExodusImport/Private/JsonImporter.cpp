#include "JsonImportPrivatePCH.h"

#include "JsonImporter.h"

#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Classes/Components/PointLightComponent.h"
#include "Engine/Classes/Components/SpotLightComponent.h"
#include "Engine/Classes/Components/DirectionalLightComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Classes/Components/StaticMeshComponent.h"
#include "Engine/Classes/Components/ChildActorComponent.h"
#include "LevelEditorViewport.h"
#include "Factories/TextureFactory.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/WorldFactory.h"

#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionConstant.h"
	
#include "RawMesh.h"

#include "DesktopPlatformModule.h"
#include "JsonObjects.h"

#include "Kismet2/KismetEditorUtilities.h"
#include "UnrealUtilities.h"
#include "JsonObjects/JsonTerrainData.h"
#include "Classes/Animation/Skeleton.h"
#include "Classes/Animation/AnimSequence.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"


#define LOCTEXT_NAMESPACE "FJsonImportModule"

using namespace JsonObjects;
using namespace UnrealUtilities;
//using namespace MaterialTools;

FString JsonImporter::getProjectImportPath() const{
	auto result = getDefaultImportPath();
	if (result.Len() && sourceBaseName.Len())
		result = FPaths::Combine(*result, *sourceBaseName);
	return result;
}

void JsonImporter::importTerrainData(JsonObjPtr jsonData, JsonId terrainId, const FString &rootPath){
	//
	JsonTerrainData terrainData;
	terrainData.load(jsonData);

	terrainDataMap.Add(terrainId, terrainData);
	//binTerrainIdMap.Add(terrainData
}

void JsonImporter::loadTerrains(const StringArray &terrains){
	FScopedSlowTask terProgress(terrains.Num(), LOCTEXT("Importing terrains", "Importing terrains"));
	terProgress.MakeDialog();
	JsonId id = 0;
	for(auto curFilename: terrains){
		auto obj = loadExternResourceFromFile(curFilename);//cur->AsObject();
		auto curId= id;
		id++;
		if (!obj.IsValid())
			continue;

		importTerrainData(obj, curId, assetRootPath);
		terProgress.EnterProgressFrame(1.0f);
	}

}

void JsonImporter::loadCubemaps(const StringArray &cubemaps){
	FScopedSlowTask texProgress(cubemaps.Num(), LOCTEXT("Importing cubemaps", "Importing cubemaps"));
	texProgress.MakeDialog();
	UE_LOG(JsonLog, Log, TEXT("Processing textures"));
	for(auto curFilename: cubemaps){
		auto obj = loadExternResourceFromFile(curFilename);
		if (!obj.IsValid())
			continue;
		importCubemap(obj, assetRootPath);
		texProgress.EnterProgressFrame(1.0f);
	}
}

void JsonImporter::loadTextures(const StringArray & textures){
	FScopedSlowTask texProgress(textures.Num(), LOCTEXT("Importing textures", "Importing textures"));
	texProgress.MakeDialog();
	UE_LOG(JsonLog, Log, TEXT("Processing textures"));
	for(auto curFilename: textures){
		auto obj = loadExternResourceFromFile(curFilename);
		if (!obj.IsValid())
			continue;
		importTexture(obj, assetRootPath);
		texProgress.EnterProgressFrame(1.0f);
	}
}

void JsonImporter::loadSkeletons(const StringArray &skeletons){
	FScopedSlowTask skelProgress(skeletons.Num(), LOCTEXT("Importing skeletons", "Importing skeletons"));
	skelProgress.MakeDialog();
	UE_LOG(JsonLog, Log, TEXT("Processing skeletons"));

	jsonSkeletons.Empty();

	for(int id = 0; id < skeletons.Num(); id++){
		const auto& curFilename = skeletons[id];
		auto obj = loadExternResourceFromFile(curFilename);
		if (!obj.IsValid()){
			continue;
		}

		JsonSkeleton jsonSkel(obj);
		jsonSkeletons.Add(id, jsonSkel);
		UE_LOG(JsonLog, Log, TEXT("Loaded json skeleotn #%d (%s)"), jsonSkel.id, *jsonSkel.name);

		skelProgress.EnterProgressFrame(1.0f);
	}
}

void JsonImporter::loadMaterials(const StringArray &materials){
	FScopedSlowTask matProgress(materials.Num(), LOCTEXT("Importing materials", "Importing materials"));
	matProgress.MakeDialog();
	UE_LOG(JsonLog, Log, TEXT("Processing materials"));
	jsonMaterials.Empty();
	int32 matId = 0;
	for(auto curFilename: materials){
		auto obj = loadExternResourceFromFile(curFilename);
		auto curId = matId;
		matId++;//hmm.... I should probably axe this?
		if (!obj.IsValid())
			continue;

		JsonMaterial jsonMat(obj);
		jsonMaterials.Add(jsonMat);
		//importMasterMaterial(obj, curId);
		if (!jsonMat.supportedShader){
			UE_LOG(JsonLog, Warning, TEXT("Material \"%s\"(id: %d) is marked as having unsupported shader \"%s\""),
				*jsonMat.name, jsonMat.id, *jsonMat.shader);
		}

		auto matInst = materialBuilder.importMaterialInstance(jsonMat, this);
		if (matInst){
			//registerMaterialInstancePath(curId, matInst->GetPathName());
			registerMaterialInstancePath(jsonMat.id, matInst->GetPathName());
		}

		//importMaterialInstance(jsonMat, curId);
		matProgress.EnterProgressFrame(1.0f);
	}
}

void JsonImporter::loadMeshes(const StringArray &meshes){
	FScopedSlowTask meshProgress(meshes.Num(), LOCTEXT("Importing materials", "Importing meshes"));
	meshProgress.MakeDialog();
	UE_LOG(JsonLog, Log, TEXT("Processing meshes"));
	int32 meshId = 0;
	for(auto curFilename: meshes){
		auto obj = loadExternResourceFromFile(curFilename);
		auto curId = meshId;
		meshId++;//and this one too...
		if (!obj.IsValid())
			continue;
		importMesh(obj, curId);
		meshProgress.EnterProgressFrame(1.0f);
	}
}

void JsonImporter::buildInstanceIdMap(InstanceToIdMap &outMap, const TArray<JsonGameObject>& objects) const{
	outMap.Empty();
	for(const auto &cur: objects){
		auto instId = cur.instanceId;
		auto objId = cur.id;
		auto found = outMap.Find(instId);
		if (found){
			UE_LOG(JsonLog, Warning, TEXT("Duplicate instance id found: %d; exisitng object id %d, new object id %d (%s)"),
				instId, *found, objId, *cur.name
			);
			outMap.Remove(instId);
		}
		outMap.Add(instId, objId);
	}
}

const JsonGameObject* JsonImporter::resolveObjectReference(const JsonObjectReference &ref,
	const InstanceToIdMap instanceMap, const TArray<JsonGameObject>& objects) const{
	if (ref.isNull)
		return nullptr;
	auto found = instanceMap.Find(ref.instanceId);
	if (!found)
		return nullptr;
	if ((*found < 0) || (*found >= objects.Num()))
		return nullptr;
	return &objects[*found];
}

void JsonImporter::processPhysicsJoint(const JsonGameObject &obj, const InstanceToIdMap &instanceMap, 
	const TArray<JsonGameObject>& objects, ImportWorkData &workData) const{
	using namespace UnrealUtilities;

	if (!obj.hasJoints())
		return;
	auto srcObj = workData.findImportedObject(obj.id);
	if (!srcObj){
		UE_LOG(JsonLog, Warning, TEXT("Src object %d not found while processing joints"), obj.id);
	}

	check(srcObj->hasComponent() || srcObj->hasActor());
	auto srcRootActor = srcObj->findRootActor();
	check(srcRootActor);
	for(int jointIndex = 0; jointIndex < obj.joints.Num(); jointIndex++){
		const JsonPhysicsJoint &curJoint = obj.joints[jointIndex];

		auto dstJsonObj = resolveObjectReference(curJoint.connectedBodyObject, instanceMap, objects);
		if (!curJoint.isConnectedToWorld() && !dstJsonObj){
			UE_LOG(JsonLog, Warning, TEXT("dst object %d not found while processing joints on %d(%d: \"%s\")"),
				curJoint.connectedBodyObject.instanceId, obj.instanceId, obj.id, *obj.name);
			continue;
		}

		if (curJoint.isSpringJointType() 
			|| curJoint.isHingeJointType() 
			|| curJoint.isConfigurableJointType() || curJoint.isConfigurableJointType()){
			UE_LOG(JsonLog, Warning, TEXT("Unsupported joint type %s at object %d(%s)"),
				*curJoint.jointType, obj.id, *obj.name);
			continue;
		}

		AActor *dstActor = nullptr;
		UPrimitiveComponent *dstComponent = nullptr;
		if (dstJsonObj){
			auto dstObj = workData.findImportedObject(dstJsonObj->id);
			if (dstObj){
				dstActor = dstObj->findRootActor();
				dstComponent = Cast<UPrimitiveComponent>(dstObj->component);
			}
		}

		auto physConstraint = NewObject<UPhysicsConstraintComponent>(srcRootActor);
		auto anchorPos = curJoint.anchor;
		auto anchorTransform = obj.getUnrealTransform(anchorPos);
		physConstraint->SetWorldTransform(anchorTransform);
		auto physObj = ImportedObject(physConstraint);

		auto jointName = FString::Printf(TEXT("joint_%d_%s"), jointIndex, *curJoint.jointType);
		physObj.setNameOrLabel(jointName);
		physObj.attachTo(srcObj);
		physObj.convertToInstanceComponent();
		physObj.fixEditorVisibility();
		//physConstraint->RegisterComponent();

		physConstraint->SetLinearBreakable(curJoint.isLinearBreakable(), unityForceToUnreal(curJoint.breakForce));
		physConstraint->SetAngularBreakable(curJoint.isAngularBreakable(), unityTorqueToUnreal(curJoint.breakTorque));
		UE_LOG(JsonLog, Log, TEXT("linear breakable: %d (%f); angular breakable: %d (%f);"),
			(int)curJoint.isLinearBreakable(), curJoint.breakForce,
			(int)curJoint.isAngularBreakable(), curJoint.breakTorque);

		auto srcActor = srcObj->findRootActor();
		auto srcComponent = Cast<UPrimitiveComponent>(srcObj->component);

		physConstraint->ConstraintActor1 = srcActor;
		if (srcComponent)
			physConstraint->OverrideComponent1 = srcComponent;
		physConstraint->ConstraintActor2 = dstActor;
		if (dstComponent)
			physConstraint->OverrideComponent2 = dstComponent;

		if (curJoint.isFixedJointType()){
			physConstraint->SetLinearXLimit(LCM_Locked, 1.0f);
			physConstraint->SetLinearYLimit(LCM_Locked, 1.0f);
			physConstraint->SetLinearZLimit(LCM_Locked, 1.0f);
			physConstraint->SetAngularSwing1Limit(ACM_Locked, 0.0f);
			physConstraint->SetAngularSwing2Limit(ACM_Locked, 0.0f);
			physConstraint->SetAngularTwistLimit(ACM_Locked, 0.0f);
		}
		else if (curJoint.isHingeJointType()){
			physConstraint->SetLinearXLimit(LCM_Locked, 1.0f);
			physConstraint->SetLinearYLimit(LCM_Locked, 1.0f);
			physConstraint->SetLinearZLimit(LCM_Locked, 1.0f);
			physConstraint->SetAngularSwing2Limit(ACM_Locked, 0.0f);
			physConstraint->SetAngularTwistLimit(ACM_Locked, 0.0f);
			physConstraint->SetAngularSwing1Limit(ACM_Free, 0.0f);

			FVector jointPos;
			FVector jointSwing1;
			FVector jointSwing2;

			physConstraint->SetConstraintReferencePosition(EConstraintFrame::Frame1, jointPos);
			physConstraint->SetConstraintReferenceOrientation(EConstraintFrame::Frame1, jointSwing1, jointSwing2);
		}
		else{
			UE_LOG(JsonLog, Warning, TEXT("Unhandled joint type %s at object %d(%s)"),
				*curJoint.jointType, obj.id, *obj.name);
		}
	}
}

void JsonImporter::processPhysicsJoints(const TArray<JsonGameObject>& objects, ImportWorkData &workData) const{
	InstanceToIdMap instanceMap;
	FScopedSlowTask progress(objects.Num(), LOCTEXT("Processing joints", "Processing joints"));
	progress.MakeDialog();
	UE_LOG(JsonLog, Log, TEXT("Processing joints"));
	for (int i = 0; i < objects.Num(); i++){
		auto srcObj = objects[i];
		if (srcObj.hasJoints()){
			if (instanceMap.Num() == 0){
				buildInstanceIdMap(instanceMap, objects);
				check(instanceMap.Num() != 0);//the map can't be empty past this point, at least one entry should be there...
			}
			processPhysicsJoint(srcObj, instanceMap, objects, workData);
		}
		progress.EnterProgressFrame(1.0f);
	}
}

void JsonImporter::loadObjects(const TArray<JsonGameObject> &objects, ImportWorkData &importData){
	FScopedSlowTask objProgress(objects.Num(), LOCTEXT("Importing objects", "Importing objects"));
	objProgress.MakeDialog();
	UE_LOG(JsonLog, Log, TEXT("Import objects"));
	int32 objId = 0;
	for(const auto &curObj: objects){
		auto curId = objId;
		objId++;
		importObject(curObj, objId, importData);
		objProgress.EnterProgressFrame(1.0f);
	}

	processPhysicsJoints(objects, importData);

	processDelayedAnimators(objects, importData);
}

void JsonImporter::importResources(const JsonExternResourceList &externRes){
	assetCommonPath = findCommonPath(externRes.resources);

	loadTextures(externRes.textures);
	loadCubemaps(externRes.cubemaps);
	loadMaterials(externRes.materials);
	loadSkeletons(externRes.skeletons);
	loadMeshes(externRes.meshes);
	importPrefabs(externRes.prefabs);
	loadTerrains(externRes.terrains);

	//loadAnimClipsDebug(externRes.animationClips);
	//loadAnimatorsDebug(externRes.animatorControllers); 
}

JsonObjPtr JsonImporter::loadExternResourceFromFile(const FString &filename) const{
	auto fullPath = FPaths::Combine(sourceExternDataPath, filename);
	return loadJsonFromFile(fullPath);
}

void JsonImporter::setupAssetPaths(const FString &jsonFilename){
	assetRootPath = FPaths::GetPath(jsonFilename);
	sourceBaseName = FPaths::GetBaseFilename(jsonFilename);
	sourceExternDataPath = FPaths::Combine(*assetRootPath, *sourceBaseName);
	assetRootPath = sourceExternDataPath;
}

void JsonImporter::registerMasterMaterialPath(int32 id, FString path){
	if (matMasterIdMap.Contains(id)){
		UE_LOG(JsonLog, Warning, TEXT("DUplicate material registration for id %d, path \"%s\""), id, *path);
	}
	matMasterIdMap.Add(id, path);
}

void JsonImporter::registerEmissiveMaterial(int32 id){
	emissiveMaterials.Add(id);
}

FString JsonImporter::getMeshPath(ResId id) const{
	auto result = meshIdMap.Find(id);
	if (result)
		return *result;
	return FString();
}

UStaticMesh* JsonImporter::loadStaticMeshById(ResId id) const{
	auto path = meshIdMap.Find(id);
	if (!path)
		return nullptr;
	auto result = LoadObject<UStaticMesh>(0, **path);
	return result;
}

void JsonImporter::registerMaterialInstancePath(int32 id, FString path){
	if (matInstIdMap.Contains(id)){
		UE_LOG(JsonLog, Warning, TEXT("Duplicate material registration for id %d, path \"%s\""), id, *path);
	}
	matInstIdMap.Add(id, path);
}

UMaterialInterface* JsonImporter::loadMaterialInterface(int32 id) const{
	return loadMaterialInstance(id);
}


USkeleton* JsonImporter::getSkeletonObject(int32 id) const{
	auto found = skeletonIdMap.Find(id);
	if (!found)
		return nullptr;
	auto result = LoadObject<USkeleton>(nullptr, **found);
	return result;
}

void JsonImporter::registerSkeleton(int32 id, USkeleton *skel){
	check(skel);
	check(id >= 0);

	if (skeletonIdMap.Contains(id)){
		UE_LOG(JsonLog, Log, TEXT("Duplicate skeleton registration for id %d"), id);
		return;
	}

	auto path = skel->GetPathName();
	skeletonIdMap.Add(id, path);
	//auto outer = skel->
}

UAnimSequence* JsonImporter::getAnimSequence(AnimClipIdKey key) const{
	auto found = animClipPaths.Find(key);
	if (!found)
		return nullptr;
	return LoadObject<UAnimSequence>(nullptr, **found);
}

void JsonImporter::registerAnimSequence(AnimClipIdKey key, UAnimSequence *sequence){
	check(sequence);
	if (animClipPaths.Contains(key)){
		UE_LOG(JsonLog, Warning, TEXT("Duplicate animation clip regsitration for skeleton %d; clip %d"), key.Key, key.Value)
		return;
	}
	auto path = sequence->GetPathName();
	animClipPaths.Add(key, path);
}
