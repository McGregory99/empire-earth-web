/*
 * GETerrainMesh and the records it holds: material lists, polygons, render vertices, point and tile
 * rows.  The mesh is about 1.5 MB, so it lives on the heap.  The engine's accessors read what the
 * layout wrote; the engine's constructor and destructor build and free a real one.
 */
#include "oracle.h"

#include <stdlib.h>
#include <string.h>

typedef float (TC *fn_f_uu_t)(const void *, unsigned long, unsigned long);
typedef unsigned char (TC *fn_b_uu_t)(const void *, unsigned long, unsigned long);
typedef void *(TC *fn_p_uu_t)(const void *, unsigned long, unsigned long);

#define MESH_ALLOC (sizeof(GETerrainMeshLayout) + 64)

static void accessors(void)
{
    GETerrainMeshLayout *mesh = (GETerrainMeshLayout *)calloc(1, MESH_ALLOC);
    GETMMaterialLayout materials[2];
    GETMPointRowLayout rows[2];
    GETMPointLayout points[3];
    GETMTileRowLayout tile_rows[2];
    GETMTileLayout tiles[3];
    GEVectorLayout normal;
    void *registered[2] = {(void *)0xA001, (void *)0xA002};
    uint32_t a = 0, b = 0;

    memset(materials, 0, sizeof(materials));
    mesh->materials.first = materials;
    mesh->materials.last = materials + 2;
    CHECK_F(((fn_u_v)oracle_export("?GetTerrainMaterialCount@GETerrainMesh@@QBEKXZ"))(mesh), 2, "GETerrainMesh.materials (a vector at 0x10) holds 0x34-byte records");
    CHECK(((fn_p_v)oracle_export("?GetTerrainMaterialList@GETerrainMesh@@QAEAAV?$vector@UGETMMaterial@GETerrainMesh@@V?$allocator@UGETMMaterial@GETerrainMesh@@@std@@@std@@XZ"))(mesh) == (void *)&mesh->materials, "GETerrainMesh.materials is at 0x10");
    mesh->waterMaterials.first = materials;
    mesh->waterMaterials.last = materials + 1;
    CHECK_F(((fn_u_v)oracle_export("?GetWaterMaterialCount@GETerrainMesh@@QBEKXZ"))(mesh), 1, "GETerrainMesh.waterMaterials (a vector at 0x30) holds 0x34-byte records");
    CHECK(((fn_p_v)oracle_export("?GetWaterMaterialList@GETerrainMesh@@QAEAAV?$vector@UGETMMaterial@GETerrainMesh@@V?$allocator@UGETMMaterial@GETerrainMesh@@@std@@@std@@XZ"))(mesh) == (void *)&mesh->waterMaterials, "GETerrainMesh.waterMaterials is at 0x30");
    CHECK(((fn_p_v)oracle_export("?GetTerrainMaterialMap@GETerrainMesh@@QAEAAV?$map@PAVGETexture@@KU?$less@PAVGETexture@@@std@@V?$allocator@U?$pair@QAVGETexture@@K@std@@@3@@std@@XZ"))(mesh) == (void *)&mesh->materialMap, "GETerrainMesh.materialMap (a std::map) is at 0x4");
    mesh->visibleRows.first = tile_rows;
    mesh->visibleRows.last = tile_rows + 2;
    CHECK_F(((fn_u_v)oracle_export("?GetNumVisibleTileRows@GETerrainMesh@@QBEKXZ"))(mesh), 2, "GETerrainMesh.visibleRows (a vector at 0x20) holds 0xc-byte records");
    mesh->registeredTextureSets.first = registered;
    mesh->registeredTextureSets.last = registered + 2;
    CHECK(((fn_p_u)oracle_export("?GetRegisteredTextureSet@GETerrainMesh@@QBEPAVGETextureSet@@K@Z"))(mesh, 1) == registered[1], "GETerrainMesh.registeredTextureSets (a vector at 0x40)");
    CHECK(((fn_p_v)oracle_export("?GetEdgeWorldPoints@GETerrainMesh@@QAEAAV?$vector@VGE3DPoint@@V?$allocator@VGE3DPoint@@@std@@@std@@XZ"))(mesh) == (void *)&mesh->edgeWorldPoints, "GETerrainMesh.edgeWorldPoints is at 0x70");
    CHECK(((fn_p_v)oracle_export("?GetEdgeRenderVertices@GETerrainMesh@@QAEAAV?$vector@PAUGETMRenderVertex@GETerrainMesh@@V?$allocator@PAUGETMRenderVertex@GETerrainMesh@@@std@@@std@@XZ"))(mesh) == (void *)&mesh->edgeRenderVertices, "GETerrainMesh.edgeRenderVertices is at 0x60");

    CHECK(((fn_p_u)oracle_export("?GetWaterColor@GETerrainMesh@@QBEABVGEColor@@KK@Z"))(mesh, 0) == (void *)&mesh->waterColor, "GETerrainMesh.waterColor is at 0x80");

    /* Point rows: one GETMPointRow per y; a row holds the points of x in [firstIndex, firstIndex + count). */
    memset(rows, 0, sizeof(rows));
    memset(points, 0, sizeof(points));
    memset(&normal, 0, sizeof(normal));
    points[1].z = 42.0f;
    points[1].normal = &normal;
    points[1].colorIndex = 7;
    rows[1].firstIndex = 10;
    rows[1].count = 3;
    rows[1].points = points;
    mesh->pointRows = rows;
    mesh->pointRowCount = 2;
    {
        CHECK_F(((fn_f_uu_t)oracle_export("?GetZ@GETerrainMesh@@QBEMKK@Z"))(mesh, 11, 1), 42.0f, "GETerrainMesh.pointRows (0x17b578) -> GETMPoint.z (0x0) at (x - firstIndex) * 0xc");
        CHECK(((fn_p_uu_t)oracle_export("?GetVertexNormal@GETerrainMesh@@QBEPAVGEVector@@KK@Z"))(mesh, 11, 1) == (void *)&normal, "GETMPoint.normal is at 0x4");
        CHECK(((fn_p_uu_t)oracle_export("?GetVertexColor@GETerrainMesh@@QBEABVGEColor@@KK@Z"))(mesh, 11, 1) == (void *)&mesh->colorTable[7], "GETerrainMesh.colorTable (0x8c, 0xc-byte entries) is indexed by GETMPoint.colorIndex");
        CHECK(((fn_b_uu_t)oracle_export("?GetIsValidPoint@GETerrainMesh@@QBE_NKK@Z"))(mesh, 11, 1) == 1 && ((fn_b_uu_t)oracle_export("?GetIsValidPoint@GETerrainMesh@@QBE_NKK@Z"))(mesh, 13, 1) == 0,
              "GETMPointRow.firstIndex (0x0) and count (0x4) bound the valid points; pointRowCount is at 0x17c6cc");
        CHECK(((fn_b_uu_t)oracle_export("?GetColorTableIndex@GETerrainMesh@@QBEEKK@Z"))(mesh, 11, 1) == 7, "GETMPoint.colorIndex is at 0x8");
    }

    /* Tile rows. */
    memset(tile_rows, 0, sizeof(tile_rows));
    memset(tiles, 0, sizeof(tiles));
    tiles[1].textureSetId = 5;
    tiles[1].waterInfo = 1;
    tile_rows[0].firstIndex = 20;
    tile_rows[0].count = 3;
    tile_rows[0].tiles = tiles;
    mesh->tileRows = tile_rows;
    mesh->tileRowCount = 2;
    {
        CHECK(((fn_b_uu_t)oracle_export("?GetTextureSetID@GETerrainMesh@@QBEKKK@Z"))(mesh, 21, 0) == 5, "GETMTile.textureSetId is at 0x2 (tileRows is at 0x17b57c)");
        CHECK(((fn_b_uu_t)oracle_export("?GetIsWaterTile@GETerrainMesh@@QBE_NKK@Z"))(mesh, 21, 0) == 1, "GETMTile.waterInfo is at 0x3");
    }

    mesh->waterVisiblePercentage = 0.5f;
    mesh->minimumZ = -3.0f;
    mesh->maximumZ = 9.0f;
    mesh->visibleTileWidth = 31;
    mesh->visibleTileHeight = 32;
    mesh->visibleBottomLeftX = 33;
    mesh->visibleBottomLeftY = 34;
    mesh->globalWaterSetId = 35;
    mesh->visiblePolyCount = 36;
    mesh->numVisibleTiles = 37;
    CHECK_F(((fn_f_v)oracle_export("?GetVisibileWaterTilePercentage@GETerrainMesh@@QBEMXZ"))(mesh), 0.5f, "GETerrainMesh.waterVisiblePercentage is at 0x17b584");
    CHECK_F(((fn_f_v)oracle_export("?GetMinimumZ@GETerrainMesh@@QBEMXZ"))(mesh), -3.0f, "GETerrainMesh.minimumZ is at 0x17b588");
    CHECK_F(((fn_f_v)oracle_export("?GetMaximumZ@GETerrainMesh@@QBEMXZ"))(mesh), 9.0f, "GETerrainMesh.maximumZ is at 0x17b58c");
    ((fn_v_pp)oracle_export("?GetTerrainVisibleTileDimensions@GETerrainMesh@@QBEXAAK0@Z"))(mesh, &a, &b);
    CHECK(a == 31 && b == 32, "GETerrainMesh.visibleTileWidth/Height are at 0x17c6ac and 0x17c6b0");
    ((fn_v_pp)oracle_export("?GetTerrainVisibleBottomLeftIndices@GETerrainMesh@@QBEXAAK0@Z"))(mesh, &a, &b);
    CHECK(a == 33 && b == 34, "GETerrainMesh.visibleBottomLeftX/Y are at 0x17c6b4 and 0x17c6b8");
    CHECK_F(((fn_u_v)oracle_export("?GetGlobalWaterSetID@GETerrainMesh@@QBEKXZ"))(mesh), 35, "GETerrainMesh.globalWaterSetId is at 0x17c6c0");
    CHECK_F(((fn_u_v)oracle_export("?GetVisiblePolyCount@GETerrainMesh@@QBEKXZ"))(mesh), 36, "GETerrainMesh.visiblePolyCount is at 0x17c6d4");
    CHECK_F(((fn_u_v)oracle_export("?GetNumVisibleTiles@GETerrainMesh@@QBEKXZ"))(mesh), 37, "GETerrainMesh.numVisibleTiles is at 0x17c6d8");

    CHECK(((fn_p_v)oracle_export("?GetTerrainRenderVertexArray@GETerrainMesh@@QAEAAY1GF@GF@UGETMRenderVertex@1@XZ"))(mesh) == (void *)mesh->renderVertices, "GETerrainMesh.renderVertices (101 x 101 x 0x24 bytes) is at 0x8d6ec");
    CHECK(((fn_p_v)oracle_export("?GetWaterRenderVertexArray@GETerrainMesh@@QAEAAY1GF@GF@UGETMRenderVertex@1@XZ"))(mesh) == (void *)mesh->waterRenderVertices, "GETerrainMesh.waterRenderVertices is at 0x121af0");
    CHECK(((fn_p_v)oracle_export("?GetTerrainPointVisibilityArray@GETerrainMesh@@QAEAAY1GF@GF@_NXZ"))(mesh) == (void *)mesh->pointVisibility, "GETerrainMesh.pointVisibility (101 x 101 bytes) is at 0x17c6e0");
    CHECK(((fn_p_v)oracle_export("?GetWaterPointVisibilityArray@GETerrainMesh@@QAEAAY1GF@GF@_NXZ"))(mesh) == (void *)mesh->waterPointVisibility, "GETerrainMesh.waterPointVisibility is at 0x17eeb9");
    CHECK(((fn_p_v)oracle_export("?GetFrontPlane@GETerrainMesh@@QBEABVGE3DPlane@@XZ"))(mesh) == (void *)&mesh->frontPlane, "GETerrainMesh.frontPlane is at 0xc8c");
    CHECK(((fn_p_v)oracle_export("?GetBackPlane@GETerrainMesh@@QBEABVGE3DPlane@@XZ"))(mesh) == (void *)&mesh->backPlane, "GETerrainMesh.backPlane is at 0xca4");
    CHECK(((fn_p_v)oracle_export("?GetLeftPlane@GETerrainMesh@@QBEABVGE3DPlane@@XZ"))(mesh) == (void *)&mesh->leftPlane, "GETerrainMesh.leftPlane is at 0xcbc");
    CHECK(((fn_p_v)oracle_export("?GetRightPlane@GETerrainMesh@@QBEABVGE3DPlane@@XZ"))(mesh) == (void *)&mesh->rightPlane, "GETerrainMesh.rightPlane is at 0xcd4");
    free(mesh);
}

static void constructed(void)
{
    GETerrainMeshLayout *mesh = (GETerrainMeshLayout *)malloc(MESH_ALLOC);
    size_t i;
    oracle_guard_fill(mesh, MESH_ALLOC);
    ((fn_v_v)oracle_export("??0GETerrainMesh@@QAE@XZ"))(mesh);
    CHECK(oracle_guard_intact(mesh, sizeof(GETerrainMeshLayout), MESH_ALLOC), "GETerrainMesh's constructor stays inside the layout");
    CHECK(mesh->vptr == oracle_vftable("GETerrainMesh"), "GETerrainMesh.vptr (0x0) is the engine's GETerrainMesh vftable");
    CHECK(FEQ(mesh->waterColor.r, 1.0f) && FEQ(mesh->colorTable[255].b, 1.0f), "GETerrainMesh's constructor makes waterColor (0x80) and colorTable (0x8c, 256 entries) white");
    CHECK(mesh->pointRows == 0 && mesh->tileRows == 0 && mesh->pointRowCount == 0, "GETerrainMesh starts without point rows or tile rows");
    for (i = 0; i < 101 * 101; i += 5000) {
        if (!(FEQ(mesh->renderVertices[i].color.r, 1.0f) && FEQ(mesh->waterRenderVertices[i].color.r, 1.0f) && FEQ(mesh->renderVertices[i].position.x, 0.0f))) {
            break;
        }
    }
    CHECK(i >= 101 * 101, "GETerrainMesh's constructor initializes every render vertex: position 0, color 1 (0x24-byte records)");
    CHECK(mesh->maximumZ < 0.0f && mesh->minimumZ > 0.0f, "GETerrainMesh starts with an inverted z range (0x17b588, 0x17b58c)");
    ((fn_v_v)oracle_export("??1GETerrainMesh@@UAE@XZ"))(mesh);
    free(mesh);
}

void oracle_terrain(void)
{
    accessors();
    constructed();
}
