#pragma once
#include "max.h"
#define I_SKIN 0x00010000
#define I_SKINIMPORTDATA 0x00020000
#define SKIN_CLASSID Class_ID(9815843, 87654)
class ISkinImportData {
public:
    virtual ~ISkinImportData() {}
    virtual BOOL AddBoneEx(INode* boneNode, BOOL update) = 0;
    virtual BOOL SetSkinBaseTm(INode* skinNode, Matrix3 objectTm, Matrix3 nodeTm) = 0;
    virtual BOOL SetBoneTm(INode* boneNode, Matrix3 objectTm, Matrix3 nodeTm) = 0;
    virtual BOOL AddWeights(INode* skinNode, int vertexID, Tab<INode*>& boneNodeList, Tab<float>& weights) = 0;
};
