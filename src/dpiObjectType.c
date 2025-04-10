//-----------------------------------------------------------------------------
// Copyright (c) 2016, 2025, Oracle and/or its affiliates.
//
// This software is dual-licensed to you under the Universal Permissive License
// (UPL) 1.0 as shown at https://oss.oracle.com/licenses/upl and Apache License
// 2.0 as shown at http://www.apache.org/licenses/LICENSE-2.0. You may choose
// either license.
//
// If you elect to accept the software under the Apache License, Version 2.0,
// the following applies:
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// dpiObjectType.c
//   Implementation of object types.
//-----------------------------------------------------------------------------

#include "dpiImpl.h"

// forward declarations of internal functions only used in this file
static int dpiObjectType__init(dpiObjectType *objType, void *handle,
        uint32_t handleType, dpiError *error);


//-----------------------------------------------------------------------------
// dpiObjectType__allocate() [INTERNAL]
//   Allocate and initialize an object type structure.
//-----------------------------------------------------------------------------
int dpiObjectType__allocate(dpiConn *conn, void *handle, uint32_t handleType,
        dpiObjectType **objType, dpiError *error)
{
    dpiObjectType *tempObjType;

    // create structure and retain reference to connection
    *objType = NULL;
    if (dpiGen__allocate(DPI_HTYPE_OBJECT_TYPE, conn->env,
            (void**) &tempObjType, error) < 0)
        return DPI_FAILURE;
    dpiGen__setRefCount(conn, error, 1);
    tempObjType->conn = conn;

    // perform initialization
    if (dpiObjectType__init(tempObjType, handle, handleType, error) < 0) {
        dpiObjectType__free(tempObjType, error);
        return DPI_FAILURE;
    }

    *objType = tempObjType;
    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// dpiObjectType__check() [INTERNAL]
//   Validate that the connection from which the object type was created is
// still connected and issue an error if it is not.
//-----------------------------------------------------------------------------
static int dpiObjectType__check(dpiObjectType *objType, const char *fnName,
        dpiError *error)
{
    if (dpiGen__startPublicFn(objType, DPI_HTYPE_OBJECT_TYPE, fnName,
            error) < 0)
        return DPI_FAILURE;
    return dpiConn__checkConnected(objType->conn, error);
}


//-----------------------------------------------------------------------------
// dpiObjectType__describe() [INTERNAL]
//   Describe the object type and store information about it. Note that a
// separate call to OCIDescribeAny() is made in order to support nested types;
// an illegal attribute value is returned if this is not done.
//-----------------------------------------------------------------------------
static int dpiObjectType__describe(dpiObjectType *objType,
        void *describeHandle, dpiError *error)
{
    void *collectionParam, *param;
    uint16_t typeCode;

    // describe the type
    if (dpiOci__describeAny(objType->conn, objType->tdo, 0, DPI_OCI_OTYPE_PTR,
            describeHandle, error) < 0)
        return DPI_FAILURE;

    // get top level parameter descriptor
    if (dpiOci__attrGet(describeHandle, DPI_OCI_HTYPE_DESCRIBE, &param, 0,
            DPI_OCI_ATTR_PARAM, "get top level parameter", error) < 0)
        return DPI_FAILURE;

    // determine type code
    if (dpiOci__attrGet(param, DPI_OCI_DTYPE_PARAM, &typeCode, 0,
            DPI_OCI_ATTR_TYPECODE, "get type code", error) < 0)
        return DPI_FAILURE;
    objType->typeCode = typeCode;

    // determine the schema of the type
    if (dpiUtils__getAttrStringWithDup("get schema", param,
            DPI_OCI_DTYPE_PARAM, DPI_OCI_ATTR_SCHEMA_NAME, &objType->schema,
            &objType->schemaLength, error) < 0)
        return DPI_FAILURE;

    // determine the name of the type
    if (dpiUtils__getAttrStringWithDup("get name", param, DPI_OCI_DTYPE_PARAM,
            DPI_OCI_ATTR_NAME, &objType->name, &objType->nameLength,
            error) < 0)
        return DPI_FAILURE;

    // determine the package name of the type
    if (objType->env->versionInfo->versionNum > 11) {
        if (dpiUtils__getAttrStringWithDup("get package name", param,
                DPI_OCI_DTYPE_PARAM, DPI_OCI_ATTR_PACKAGE_NAME,
                &objType->packageName, &objType->packageNameLength, error) < 0)
            return DPI_FAILURE;
    }

    // determine the number of attributes
    if (dpiOci__attrGet(param, DPI_OCI_DTYPE_PARAM,
            (void*) &objType->numAttributes, 0, DPI_OCI_ATTR_NUM_TYPE_ATTRS,
            "get number of attributes", error) < 0)
        return DPI_FAILURE;

    // if a collection, need to determine the element type
    if (typeCode == DPI_SQLT_NCO) {
        objType->isCollection = 1;

        // acquire collection parameter descriptor
        if (dpiOci__attrGet(param, DPI_OCI_DTYPE_PARAM, &collectionParam, 0,
                DPI_OCI_ATTR_COLLECTION_ELEMENT, "get collection descriptor",
                error) < 0)
            return DPI_FAILURE;

        // determine type of element
        if (dpiOracleType__populateTypeInfo(objType->conn, collectionParam,
                DPI_OCI_DTYPE_PARAM, &objType->elementTypeInfo, error) < 0)
            return DPI_FAILURE;

    }

    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// dpiObjectType__free() [INTERNAL]
//   Free the memory for an object type.
//-----------------------------------------------------------------------------
void dpiObjectType__free(dpiObjectType *objType, dpiError *error)
{
    if (objType->conn) {
        dpiGen__setRefCount(objType->conn, error, -1);
        objType->conn = NULL;
    }
    if (objType->elementTypeInfo.objectType) {
        dpiGen__setRefCount(objType->elementTypeInfo.objectType, error, -1);
        objType->elementTypeInfo.objectType = NULL;
    }
    if (objType->schema) {
        dpiUtils__freeMemory((void*) objType->schema);
        objType->schema = NULL;
    }
    if (objType->name) {
        dpiUtils__freeMemory((void*) objType->name);
        objType->name = NULL;
    }
    if (objType->packageName) {
        dpiUtils__freeMemory((void*) objType->packageName);
        objType->packageName = NULL;
    }
    dpiUtils__freeMemory(objType);
}


//-----------------------------------------------------------------------------
// dpiObjectType__init() [INTERNAL]
//   Initialize the object type.
//-----------------------------------------------------------------------------
static int dpiObjectType__init(dpiObjectType *objType, void *handle,
        uint32_t handleType, dpiError *error)
{
    void *describeHandle, *tdoReference;

    // retrieve TDO of the parameter and pin it in the cache
    if (dpiOci__attrGet(handle, handleType, (void*) &tdoReference, 0,
            DPI_OCI_ATTR_REF_TDO, "get TDO reference", error) < 0)
        return DPI_FAILURE;
    if (dpiOci__objectPin(objType->env->handle, tdoReference, &objType->tdo,
            error) < 0)
        return DPI_FAILURE;

    // acquire a describe handle
    if (dpiOci__handleAlloc(objType->env->handle, &describeHandle,
            DPI_OCI_HTYPE_DESCRIBE, "allocate describe handle", error) < 0)
        return DPI_FAILURE;

    // describe the type
    if (dpiObjectType__describe(objType, describeHandle, error) < 0) {
        dpiOci__handleFree(describeHandle, DPI_OCI_HTYPE_DESCRIBE);
        return DPI_FAILURE;
    }

    // free the describe handle
    dpiOci__handleFree(describeHandle, DPI_OCI_HTYPE_DESCRIBE);

    return DPI_SUCCESS;
}


//-----------------------------------------------------------------------------
// dpiObjectType__isXmlType() [INTERNAL]
//   Returns a boolean indicating if the object type in question refers to the
// type SYS.XMLTYPE.
//-----------------------------------------------------------------------------
int dpiObjectType__isXmlType(dpiObjectType *objType)
{
    static const char *schema = "SYS", *name = "XMLTYPE";
    size_t schemaLength, nameLength;

    schemaLength = strlen(schema);
    nameLength = strlen(name);
    return (objType->schemaLength == schemaLength &&
            strncmp(objType->schema, schema, schemaLength) == 0 &&
            objType->nameLength == nameLength &&
            strncmp(objType->name, name, nameLength) == 0);
}


//-----------------------------------------------------------------------------
// dpiObjectType_addRef() [PUBLIC]
//   Add a reference to the object type.
//-----------------------------------------------------------------------------
int dpiObjectType_addRef(dpiObjectType *objType)
{
    return dpiGen__addRef(objType, DPI_HTYPE_OBJECT_TYPE, __func__);
}


//-----------------------------------------------------------------------------
// dpiObjectType_createObject() [PUBLIC]
//   Create a new object of the specified type and return it. Return NULL on
// error.
//-----------------------------------------------------------------------------
int dpiObjectType_createObject(dpiObjectType *objType, dpiObject **obj)
{
    dpiError error;
    int status;

    // validate parameters
    if (dpiObjectType__check(objType, __func__, &error) < 0)
        return dpiGen__endPublicFn(objType, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(objType, obj)
    status = dpiObject__allocate(objType, NULL, NULL, NULL, obj, &error);
    return dpiGen__endPublicFn(objType, status, &error);
}


//-----------------------------------------------------------------------------
// dpiObjectType_getAttributes() [PUBLIC]
//   Get the attributes for the object type in the provided array.
//-----------------------------------------------------------------------------
int dpiObjectType_getAttributes(dpiObjectType *objType, uint16_t numAttributes,
        dpiObjectAttr **attributes)
{
    void *topLevelParam, *attrListParam, *attrParam, *describeHandle;
    dpiError error;
    uint16_t i;

    // validate object type and the number of attributes
    if (dpiObjectType__check(objType, __func__, &error) < 0)
        return dpiGen__endPublicFn(objType, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(objType, attributes)
    if (numAttributes < objType->numAttributes) {
        dpiError__set(&error, "get attributes", DPI_ERR_ARRAY_SIZE_TOO_SMALL,
                numAttributes);
        return dpiGen__endPublicFn(objType, DPI_FAILURE, &error);
    }
    if (numAttributes == 0)
        return dpiGen__endPublicFn(objType, DPI_SUCCESS, &error);

    // acquire a describe handle
    if (dpiOci__handleAlloc(objType->env->handle, &describeHandle,
            DPI_OCI_HTYPE_DESCRIBE, "allocate describe handle", &error) < 0)
        return dpiGen__endPublicFn(objType, DPI_FAILURE, &error);

    // describe the type
    if (dpiOci__describeAny(objType->conn, objType->tdo, 0, DPI_OCI_OTYPE_PTR,
            describeHandle, &error) < 0) {
        dpiOci__handleFree(describeHandle, DPI_OCI_HTYPE_DESCRIBE);
        return dpiGen__endPublicFn(objType, DPI_FAILURE, &error);
    }

    // get the top level parameter descriptor
    if (dpiOci__attrGet(describeHandle, DPI_OCI_HTYPE_DESCRIBE, &topLevelParam,
            0, DPI_OCI_ATTR_PARAM, "get top level param", &error) < 0) {
        dpiOci__handleFree(describeHandle, DPI_OCI_HTYPE_DESCRIBE);
        return dpiGen__endPublicFn(objType, DPI_FAILURE, &error);
    }

    // get the attribute list parameter descriptor
    if (dpiOci__attrGet(topLevelParam, DPI_OCI_DTYPE_PARAM,
            (void*) &attrListParam, 0, DPI_OCI_ATTR_LIST_TYPE_ATTRS,
            "get attr list param", &error) < 0) {
        dpiOci__handleFree(describeHandle, DPI_OCI_HTYPE_DESCRIBE);
        return dpiGen__endPublicFn(objType, DPI_FAILURE, &error);
    }

    // create attribute structure for each attribute
    for (i = 0; i < objType->numAttributes; i++) {
        if (dpiOci__paramGet(attrListParam, DPI_OCI_DTYPE_PARAM, &attrParam,
                (uint32_t) i + 1, "get attribute param", &error) < 0) {
            dpiOci__handleFree(describeHandle, DPI_OCI_HTYPE_DESCRIBE);
            return dpiGen__endPublicFn(objType, DPI_FAILURE, &error);
        }
        if (dpiObjectAttr__allocate(objType, attrParam, &attributes[i],
                &error) < 0) {
            dpiOci__handleFree(describeHandle, DPI_OCI_HTYPE_DESCRIBE);
            return dpiGen__endPublicFn(objType, DPI_FAILURE, &error);
        }
    }

    // free the describe handle
    dpiOci__handleFree(describeHandle, DPI_OCI_HTYPE_DESCRIBE);

    return dpiGen__endPublicFn(objType, DPI_SUCCESS, &error);
}


//-----------------------------------------------------------------------------
// dpiObjectType_getInfo() [PUBLIC]
//   Return information about the object type.
//-----------------------------------------------------------------------------
int dpiObjectType_getInfo(dpiObjectType *objType, dpiObjectTypeInfo *info)
{
    dpiError error;

    if (dpiGen__startPublicFn(objType, DPI_HTYPE_OBJECT_TYPE, __func__,
            &error) < 0)
        return dpiGen__endPublicFn(objType, DPI_FAILURE, &error);
    DPI_CHECK_PTR_NOT_NULL(objType, info)
    info->name = objType->name;
    info->nameLength = objType->nameLength;
    info->packageName = objType->packageName;
    info->packageNameLength = objType->packageNameLength;
    info->schema = objType->schema;
    info->schemaLength = objType->schemaLength;
    info->isCollection = objType->isCollection;
    info->numAttributes = objType->numAttributes;

    // the size of the dpiDataTypeInfo structure changed in version 5.1 and
    // again in 5.2; this check and memcpy() for older versions can be removed
    // once 6.0 is released
    if (objType->env->context->dpiMinorVersion > 1) {
        info->elementTypeInfo = objType->elementTypeInfo;
    } else if (objType->env->context->dpiMinorVersion == 1) {
        memcpy(&info->elementTypeInfo, &objType->elementTypeInfo,
                sizeof(dpiDataTypeInfo__v51));
    } else {
        memcpy(&info->elementTypeInfo, &objType->elementTypeInfo,
                sizeof(dpiDataTypeInfo__v50));
    }

    return dpiGen__endPublicFn(objType, DPI_SUCCESS, &error);
}


//-----------------------------------------------------------------------------
// dpiObjectType_release() [PUBLIC]
//   Release a reference to the object type.
//-----------------------------------------------------------------------------
int dpiObjectType_release(dpiObjectType *objType)
{
    return dpiGen__release(objType, DPI_HTYPE_OBJECT_TYPE, __func__);
}
