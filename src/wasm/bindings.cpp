/*
 * Archivo: src/wasm/bindings.cpp
 * Proposito: Interfaz plana C para comunicacion entre SimulationWorld (C++) y JavaScript/WebAssembly.
 * Declaracion de transparencia academica:
 *   Codigo asistido/generado mediante inteligencia artificial: 100% (plantilla base para revision y adaptacion).
 */

#include "../engine/SimulationEngine.hpp"
#include <emscripten.h>

static Engine::SimulationWorld g_world;

extern "C" {

EMSCRIPTEN_KEEPALIVE void init_sandbox(int w, int h) { g_world.loadSandbox(w, h); }
EMSCRIPTEN_KEEPALIVE void init_xor(int a, int b) { g_world.loadXorGate(a != 0, b != 0); }
EMSCRIPTEN_KEEPALIVE void init_blank_map(int w, int h) { g_world.initBlankMap(w, h); }

EMSCRIPTEN_KEEPALIVE void step_batch(int steps, int recordUndo) { g_world.stepBatch(steps, recordUndo != 0); }
EMSCRIPTEN_KEEPALIVE void begin_edit_transaction() { g_world.beginEditTransaction(); }
EMSCRIPTEN_KEEPALIVE void commit_edit_transaction() { g_world.commitEditTransaction(); }
EMSCRIPTEN_KEEPALIVE int undo_tick() { return g_world.undo() ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE int redo_tick() { return g_world.redo() ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE int jump_to_history_node(int nodeId) { return g_world.jumpToHistoryNode(nodeId) ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE int32_t* get_history_tree_ptr() { return g_world.getHistoryTreePtr(); }
EMSCRIPTEN_KEEPALIVE int get_history_node_count() { return g_world.getHistoryNodeCount(); }

EMSCRIPTEN_KEEPALIVE void dispatch_troops(int x, int y, int dir, float fraction) {
    g_world.enqueueTroopDispatch(x, y, dir, fraction);
}

EMSCRIPTEN_KEEPALIVE void paint_cell_raw(int x, int y, int mode, int teamId, int auxParam) {
    g_world.paintCellRaw(x, y, mode, teamId, auxParam);
}
EMSCRIPTEN_KEEPALIVE void set_cell_exact(int x, int y, int terrain, int teamId, int troopsFP, int growth, int flowDir) {
    g_world.setCellExact(x, y, terrain, teamId, troopsFP, growth, flowDir);
}
EMSCRIPTEN_KEEPALIVE void erase_nation_at(int x, int y) { g_world.eraseNationAt(x, y); }
EMSCRIPTEN_KEEPALIVE void flood_fill(int x, int y, int mode, int teamId, int auxParam) {
    g_world.floodFill(x, y, mode, teamId, auxParam);
}
EMSCRIPTEN_KEEPALIVE void paint_line(int x0, int y0, int x1, int y1, int mode, int teamId, int auxParam) {
    g_world.paintLine(x0, y0, x1, y1, mode, teamId, auxParam);
}
EMSCRIPTEN_KEEPALIVE void paint_rect(int x0, int y0, int x1, int y1, int mode, int teamId, int auxParam, int filled) {
    g_world.paintRect(x0, y0, x1, y1, mode, teamId, auxParam, filled != 0);
}
EMSCRIPTEN_KEEPALIVE void paint_circle(int cx, int cy, int r, int mode, int teamId, int auxParam, int filled) {
    g_world.paintCircle(cx, cy, r, mode, teamId, auxParam, filled != 0);
}

EMSCRIPTEN_KEEPALIVE int create_team() { return g_world.createTeam(); }
EMSCRIPTEN_KEEPALIVE void delete_team(int teamId) { g_world.deleteTeam(teamId); }
EMSCRIPTEN_KEEPALIVE void set_team_color(int teamId, int r, int g, int b) { g_world.setTeamColor(teamId, r, g, b); }
EMSCRIPTEN_KEEPALIVE uint32_t get_team_color_rgba(int teamId) { return g_world.getTeamColorRGBA(teamId); }
EMSCRIPTEN_KEEPALIVE void set_team_config(
    int teamId, int d0, int d1, int d2, int d3,
    float tau, int emissionPeriod, int maxArea,
    float friction, int multiDir, float regenRate,
    float maxTroops, int organic) {
    g_world.setTeamConfig(teamId, d0, d1, d2, d3, tau, emissionPeriod, maxArea,
                          friction, multiDir != 0, regenRate, maxTroops, organic != 0);
}
EMSCRIPTEN_KEEPALIVE int get_team_count() { return g_world.getTeamCount(); }
EMSCRIPTEN_KEEPALIVE int is_team_active(int teamId) { return g_world.isTeamActive(teamId) ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE float get_team_config_param(int teamId, int idx) { return g_world.getTeamConfigParam(teamId, idx); }
EMSCRIPTEN_KEEPALIVE int get_team_area(int teamId) { return g_world.getTeamArea(teamId); }
EMSCRIPTEN_KEEPALIVE float get_team_troops(int teamId) { return g_world.getTeamTroops(teamId); }
EMSCRIPTEN_KEEPALIVE int get_team_nation_count(int teamId) { return g_world.getTeamNationCount(teamId); }

EMSCRIPTEN_KEEPALIVE int get_width() { return g_world.getWidth(); }
EMSCRIPTEN_KEEPALIVE int get_height() { return g_world.getHeight(); }
EMSCRIPTEN_KEEPALIVE int get_tick() { return g_world.getTick(); }
EMSCRIPTEN_KEEPALIVE int get_nation_count() { return g_world.getNationCount(); }
EMSCRIPTEN_KEEPALIVE int get_active_cell_count() { return g_world.getActiveCellCount(); }
EMSCRIPTEN_KEEPALIVE uint32_t get_render_version() { return g_world.getRenderVersion(); }

EMSCRIPTEN_KEEPALIVE uint32_t* get_pixel_buffer() { return g_world.getPixelBufferPtr(); }
EMSCRIPTEN_KEEPALIVE uint16_t* get_meta_buffer() { return g_world.getMetaBufferPtr(); }
EMSCRIPTEN_KEEPALIVE float* get_graph_nodes_ptr() { return g_world.getGraphNodesPtr(); }
EMSCRIPTEN_KEEPALIVE int get_graph_node_count() { return g_world.getGraphNodeCount(); }
EMSCRIPTEN_KEEPALIVE float* get_graph_edges_ptr() { return g_world.getGraphEdgesPtr(); }
EMSCRIPTEN_KEEPALIVE int get_graph_edge_count() { return g_world.getGraphEdgeCount(); }

EMSCRIPTEN_KEEPALIVE int get_cell_nation_size(int x, int y) { return g_world.getCellNationSize(x, y); }
EMSCRIPTEN_KEEPALIVE int get_cell_nation_id(int x, int y) { return g_world.getCellNationId(x, y); }
EMSCRIPTEN_KEEPALIVE int get_cell_troops_fp(int x, int y) { return g_world.getCellTroopsFP(x, y); }
EMSCRIPTEN_KEEPALIVE int get_cell_terrain(int x, int y) { return g_world.getCellTerrain(x, y); }
EMSCRIPTEN_KEEPALIVE int get_cell_team(int x, int y) { return g_world.getCellTeam(x, y); }
EMSCRIPTEN_KEEPALIVE int get_cell_growth_factor(int x, int y) { return g_world.getCellGrowthFactor(x, y); }
EMSCRIPTEN_KEEPALIVE int get_cell_flow_dir(int x, int y) { return g_world.getCellFlowDir(x, y); }

}