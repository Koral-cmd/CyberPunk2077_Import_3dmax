#pragma once
#include "max.h"
enum GetSetMethod { CTRL_RELATIVE, CTRL_ABSOLUTE };
class Control : public ReferenceTarget {
public:
    virtual void SetValue(TimeValue, void*, int commit = 1, GetSetMethod = CTRL_ABSOLUTE) { (void)commit; }
    virtual BOOL SetPositionController(Control*) { return TRUE; }
    virtual BOOL SetRotationController(Control*) { return TRUE; }
    virtual Control* GetPositionController() { return nullptr; }
    virtual Control* GetRotationController() { return nullptr; }
};
