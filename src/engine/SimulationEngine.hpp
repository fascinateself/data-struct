/*
 * Archivo: src/engine/SimulationEngine.hpp
 * Proposito: Motor determinista del automata celular con aritmetica de punto fijo entera,
 *            multiplicador growthFactor por celda, LUT de terrenos, expansion organica,
 *            lista activa de fronteras, arbol N-ario de historial y bufer RGBA de 32 bits.
 * Declaracion de transparencia academica:
 *   Codigo asistido/generado mediante inteligencia artificial: 100% (plantilla base para revision y adaptacion).
 */

#ifndef SIMULATION_ENGINE_HPP
#define SIMULATION_ENGINE_HPP

#include "../core/LinearADTs.hpp"
#include "../core/HistoryTree.hpp"
#include <cstdint>

namespace Engine {

// Constantes de aritmetica de punto fijo entera (potencias de 2 para desplazamientos O(1))
static constexpr int32_t FP_SHIFT = 10;
static constexpr int32_t FP_ONE = 1 << FP_SHIFT;          // 1024 equivale a 1.0
static constexpr int32_t GROWTH_SHIFT = 6;
static constexpr int32_t GROWTH_BASE = 1 << GROWTH_SHIFT; // 64 equivale a 1.0x en uint8_t

enum Direction : int {
    DIR_UP = 0,
    DIR_LEFT = 1,
    DIR_DOWN = 2,
    DIR_RIGHT = 3
};

static constexpr int DX[4] = { 0, -1, 0, 1 };
static constexpr int DY[4] = { -1, 0, 1, 0 };

inline int oppositeDirection(int dir) { return (dir + 2) % 4; }
inline int absInt(int v) { return v < 0 ? -v : v; }

inline uint32_t packRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(g) << 8)  |
           static_cast<uint32_t>(r);
}

// =========================================================================
// Tabla de Busqueda de Terrenos (LUT en Cache L1: 80 bytes en total)
// =========================================================================

enum TerrainType : uint8_t {
    TERRAIN_N = 0, // No habitable
    TERRAIN_H = 1, // Colonizable normal
    TERRAIN_T = 2, // Transito estandar
    TERRAIN_O = 3, // Objetivo colonizable
    TERRAIN_R = 4, // Fertil / Recurso (2.0x regeneracion, menor costo)
    TERRAIN_F = 5, // Fortificada / Bastion (2.0x umbral defensivo tau)
    TERRAIN_S = 6, // Pantano (alto costo, 0.5x regeneracion y capacidad)
    TERRAIN_W = 7  // Corriente / Diodo unidireccional (transito con direccion fija)
};

struct TerrainProps {
    uint8_t isHabitable;
    uint8_t isColonizable;
    uint16_t costFP;      // Costo de entrada (escala FP_ONE = 1024)
    uint16_t regenMultFP; // Multiplicador de regeneracion (escala FP_ONE)
    uint16_t defMultFP;   // Multiplicador sobre umbral tau (escala FP_ONE)
    uint16_t capMultFP;   // Multiplicador sobre capacidad maxima de tropas (escala FP_ONE)
};

static constexpr TerrainProps TERRAIN_LUT[8] = {
    { 0, 0, 1024,    0, 1024,    0 }, // 0: N
    { 1, 1, 1024, 1024, 1024, 1024 }, // 1: H
    { 1, 0, 1024,    0, 1024,    0 }, // 2: T
    { 1, 1, 1024, 1024, 1024, 1024 }, // 3: O
    { 1, 1,  768, 2048, 1024, 1536 }, // 4: R (Fertil)
    { 1, 1, 1536, 1024, 2048, 1024 }, // 5: F (Fortificada)
    { 1, 1, 2048,  512, 1024,  512 }, // 6: S (Pantano)
    { 1, 0, 1024,    0, 1024,    0 }  // 7: W (Corriente / Diodo)
};

// =========================================================================
// Estructuras de Dominio Empaquetadas (Alineacion Optima en Memoria)
// =========================================================================

struct TeamConfig {
    bool active;
    bool multiDirTransit;
    bool organicExpansion;      // Activa expansion por costo/fertilidad y cohesion de frontera
    int priorityOrder[4];
    int32_t conquestThresholdFP;// Umbral tau en punto fijo (128 = 12.5%)
    int32_t emissionPeriod;     // -1 = disparo unico, 0 = apagado, P >= 1 = cada P ticks
    int32_t maxAreaLimit;       // 0 = ilimitado
    int32_t expansionFrictionFP;// Factor beta escalado por 1024 (ej. 4 = 0.0039)
    int32_t troopRegenFP;       // Regeneracion base por tick en punto fijo (256 = 0.25)
    int32_t maxCellTroopsFP;    // Capacidad maxima base por celda (10240 = 10.0)
    uint32_t colorRGBA;         // Color empaquetado para escritura directa en pixelBuffer

    TeamConfig()
        : active(false), multiDirTransit(false), organicExpansion(true),
          conquestThresholdFP(128), emissionPeriod(0), maxAreaLimit(0),
          expansionFrictionFP(4), troopRegenFP(256), maxCellTroopsFP(10240),
          colorRGBA(packRGBA(100, 116, 139)) {
        priorityOrder[0] = DIR_UP;
        priorityOrder[1] = DIR_LEFT;
        priorityOrder[2] = DIR_DOWN;
        priorityOrder[3] = DIR_RIGHT;
    }
};

// Estructura Cell ordenada por tamano de campos para minimizar padding (44 bytes)
struct Cell {
    int32_t localTroopsFP;
    int32_t nationId;
    int32_t transitStrengthFP[4];
    uint16_t teamId;
    uint16_t transitTeam[4];
    uint16_t expansionAccumFP;
    int16_t emissionCooldown;
    uint8_t terrain;
    uint8_t growthFactor;       // Multiplicador individual (64 = 1.0x, 128 = 2.0x, 32 = 0.5x)
    uint8_t flowDir;            // Direccion forzada para celdas W (0..3)
    uint8_t flags;              // Bit 0: hasEmittedOnce

    Cell()
        : localTroopsFP(0), nationId(0), teamId(0), expansionAccumFP(0),
          emissionCooldown(0), terrain(TERRAIN_H), growthFactor(GROWTH_BASE),
          flowDir(DIR_RIGHT), flags(0) {
        for (int d = 0; d < 4; ++d) {
            transitStrengthFP[d] = 0;
            transitTeam[d] = 0;
        }
    }

    inline bool hasAnyTransit() const {
        return transitTeam[0] != 0 || transitTeam[1] != 0 ||
               transitTeam[2] != 0 || transitTeam[3] != 0;
    }

    inline void clearTransit() {
        for (int d = 0; d < 4; ++d) {
            transitTeam[d] = 0;
            transitStrengthFP[d] = 0;
        }
    }

    // Comparacion exacta de estado persistente para compresion de deltas en HistoryTree
    bool operator==(const Cell& o) const {
        if (terrain != o.terrain || growthFactor != o.growthFactor ||
            flowDir != o.flowDir || flags != o.flags ||
            teamId != o.teamId || localTroopsFP != o.localTroopsFP ||
            expansionAccumFP != o.expansionAccumFP || emissionCooldown != o.emissionCooldown) {
            return false;
        }
        for (int d = 0; d < 4; ++d) {
            if (transitTeam[d] != o.transitTeam[d] || transitStrengthFP[d] != o.transitStrengthFP[d]) {
                return false;
            }
        }
        return true;
    }
};

struct Impulse {
    uint16_t teamId;
    int32_t strengthFP;
    uint8_t moveDir;
    uint8_t fromTransit;

    Impulse() : teamId(0), strengthFP(0), moveDir(DIR_UP), fromTransit(0) {}
    Impulse(uint16_t t, int32_t w, uint8_t d, uint8_t ft)
        : teamId(t), strengthFP(w), moveDir(d), fromTransit(ft) {}
};

struct CellIntentBuffer {
    Impulse list[16];
    int count;

    CellIntentBuffer() : count(0) {}
};

struct TeamForce {
    uint16_t teamId;
    int64_t totalStrengthFP;
    uint8_t primaryDir;
    uint8_t fromTransit;

    TeamForce() : teamId(0), totalStrengthFP(0), primaryDir(DIR_UP), fromTransit(0) {}
    TeamForce(uint16_t t, int64_t w, uint8_t d, uint8_t ft)
        : teamId(t), totalStrengthFP(w), primaryDir(d), fromTransit(ft) {}

    bool operator<(const TeamForce& other) const {
        if (totalStrengthFP != other.totalStrengthFP) {
            return totalStrengthFP < other.totalStrengthFP;
        }
        return teamId > other.teamId;
    }
};

struct PlayerOrder {
    int x;
    int y;
    int dir;
    int32_t fractionFP; // Escala FP_ONE = 1024

    PlayerOrder() : x(0), y(0), dir(DIR_RIGHT), fractionFP(512) {}
    PlayerOrder(int px, int py, int d, int32_t f) : x(px), y(py), dir(d), fractionFP(f) {}
};

struct NeighborList {
    int ids[16];
    int count;

    NeighborList() : count(0) {}
    bool contains(int id) const {
        for (int i = 0; i < count; ++i) {
            if (ids[i] == id) return true;
        }
        return false;
    }
    void add(int id) {
        if (count < 16 && !contains(id)) ids[count++] = id;
    }
};

// =========================================================================
// Clase Principal del Motor de Simulacion
// =========================================================================

class SimulationWorld {
private:
    int width;
    int height;
    int currentTick;
    int totalNations;
    uint32_t renderVersion;
    bool topologyDirty;

    Core::DynamicArray<Cell> currentGrid;
    Core::DynamicArray<Cell> nextGrid;
    Core::DynamicArray<CellIntentBuffer> intentGrid;

    // Estructuras de optimizacion por lista activa y escritura diferencial (stamps en O(1))
    Core::DynamicArray<int> activeCells;
    Core::DynamicArray<int> nextActiveCells;
    Core::DynamicArray<uint32_t> activeStamp;
    uint32_t curActiveStamp;

    Core::DynamicArray<int> dirtyIndices;
    Core::DynamicArray<uint32_t> dirtyStamp;
    uint32_t curDirtyStamp;

    Core::DynamicArray<int> touchedIntents;
    Core::DynamicArray<uint32_t> intentStamp;
    uint32_t curIntentStamp;

    // Arreglos por nacion
    Core::DynamicArray<int> nationSizes;
    Core::DynamicArray<int64_t> nationTroopsFP;
    Core::DynamicArray<uint16_t> nationTeams;
    Core::DynamicArray<float> nationCentroidX;
    Core::DynamicArray<float> nationCentroidY;

    // Configuraciones y telemetria por equipo
    Core::DynamicArray<TeamConfig> teamConfigs;
    Core::DynamicArray<int> teamAreas;
    Core::DynamicArray<int64_t> teamTroopsFP;
    Core::DynamicArray<int> teamNationCounts;

    // Buferes de memoria compartida con JavaScript (WASM)
    Core::DynamicArray<uint32_t> pixelBuffer;  // 1 uint32 RGBA por celda para ImageData
    Core::DynamicArray<uint16_t> metaBuffer;   // 4 uint16 por celda para LOD y zoom alto
    Core::DynamicArray<float> graphNodesBuf;   // 5 floats por nodo
    Core::DynamicArray<float> graphEdgesBuf;   // 4 floats por arista

    // Arbol N-ario de historial y cola circular de ordenes del jugador
    Core::HistoryTree<Cell> historyTree;
    Core::CircularQueue<PlayerOrder> playerOrderQueue;

    inline int index(int x, int y) const { return y * width + x; }
    inline bool inBounds(int x, int y) const { return x >= 0 && x < width && y >= 0 && y < height; }

    inline const TeamConfig& getConfigSafe(uint16_t teamId) const {
        return (teamId < teamConfigs.size()) ? teamConfigs[teamId] : teamConfigs[0];
    }

    // Marca una celda como activa para el siguiente tick en O(1)
    inline void markActiveNext(int idx) {
        if (activeStamp[idx] != curActiveStamp) {
            activeStamp[idx] = curActiveStamp;
            nextActiveCells.pushBack(idx);
        }
    }

    inline void markCellAndNeighborsActive(int idx) {
        markActiveNext(idx);
        int cx = idx % width;
        int cy = idx / width;
        for (int d = 0; d < 4; ++d) {
            int nx = cx + DX[d];
            int ny = cy + DY[d];
            if (inBounds(nx, ny)) {
                markActiveNext(index(nx, ny));
            }
        }
    }

    // Copia perezosa a nextGrid unicamente para las celdas que cambian en este tick. O(1).
    inline Cell& ensureDirty(int idx) {
        if (dirtyStamp[idx] != curDirtyStamp) {
            dirtyStamp[idx] = curDirtyStamp;
            nextGrid[idx] = currentGrid[idx];
            dirtyIndices.pushBack(idx);
        }
        return nextGrid[idx];
    }

    // Registra una intencion hacia la celda destino en O(1)
    inline void addIntent(int nx, int ny, const Impulse& imp) {
        int nIdx = index(nx, ny);
        if (intentStamp[nIdx] != curIntentStamp) {
            intentStamp[nIdx] = curIntentStamp;
            intentGrid[nIdx].count = 0;
            touchedIntents.pushBack(nIdx);
        }
        CellIntentBuffer& buf = intentGrid[nIdx];
        if (buf.count < 16) {
            buf.list[buf.count++] = imp;
        }
    }

    void assignDefaultTeamColor(uint16_t teamId) {
        static const uint32_t PALETTE[6] = {
            packRGBA(37, 99, 235),  // 1: Azul
            packRGBA(220, 38, 38),  // 2: Rojo
            packRGBA(5, 150, 105),  // 3: Verde
            packRGBA(124, 58, 237), // 4: Violeta
            packRGBA(217, 119, 6),  // 5: Ambar
            packRGBA(8, 145, 178)   // 6: Cian
        };
        if (teamId >= 1 && teamId <= 6) {
            teamConfigs[teamId].colorRGBA = PALETTE[teamId - 1];
        } else {
            // Color determinista por mezcla entera
            uint32_t h = static_cast<uint32_t>(teamId) * 2654435761u;
            uint8_t r = 60 + ((h >> 16) % 165);
            uint8_t g = 60 + ((h >> 8) % 165);
            uint8_t b = 60 + (h % 165);
            teamConfigs[teamId].colorRGBA = packRGBA(r, g, b);
        }
    }

    void ensureTeamExists(uint16_t teamId) {
        while (teamConfigs.size() <= static_cast<int>(teamId)) {
            uint16_t id = static_cast<uint16_t>(teamConfigs.size());
            teamConfigs.pushBack(TeamConfig());
            teamAreas.pushBack(0);
            teamTroopsFP.pushBack(0);
            teamNationCounts.pushBack(0);
            if (id > 0) assignDefaultTeamColor(id);
        }
        if (teamId > 0) {
            teamConfigs[teamId].active = true;
        }
    }

    // Actualiza el pixel RGBA de 32 bits y los metadatos LOD de una celda en O(1)
    inline void updateCellVisual(int i) {
        const Cell& c = currentGrid[i];
        uint32_t pxColor = packRGBA(24, 34, 52); // N por defecto

        uint16_t displayTeam = c.teamId;
        uint16_t flags = 0;
        int32_t displayVal = c.localTroopsFP >> FP_SHIFT;

        if (TERRAIN_LUT[c.terrain].isColonizable) {
            if (c.teamId > 0) {
                pxColor = getConfigSafe(c.teamId).colorRGBA;
            } else {
                // Modulacion sutil de luminosidad segun growthFactor individual (base 64)
                int delta = (static_cast<int>(c.growthFactor) - 64) / 3;
                int r = 226, g = 232, b = 240; // H
                if (c.terrain == TERRAIN_O)      { r = 167; g = 243; b = 208; }
                else if (c.terrain == TERRAIN_R) { r = 187; g = 247; b = 208; }
                else if (c.terrain == TERRAIN_F) { r = 148; g = 163; b = 184; }
                else if (c.terrain == TERRAIN_S) { r = 163; g = 177; b = 138; }

                r = r + delta; if (r < 40) r = 40; if (r > 255) r = 255;
                g = g + delta; if (g < 40) g = 40; if (g > 255) g = 255;
                b = b + delta; if (b < 40) b = 40; if (b > 255) b = 255;
                pxColor = packRGBA(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
            }
        } else if (TERRAIN_LUT[c.terrain].isHabitable) {
            // Terreno T o W
            pxColor = (c.terrain == TERRAIN_W) ? packRGBA(125, 211, 252) : packRGBA(253, 230, 138);
            int32_t maxStr = 0;
            int32_t sumStr = 0;
            for (int d = 0; d < 4; ++d) {
                if (c.transitTeam[d] != 0) {
                    flags |= static_cast<uint16_t>(1 | (1 << (d + 1)));
                    sumStr += c.transitStrengthFP[d];
                    if (c.transitStrengthFP[d] > maxStr) {
                        maxStr = c.transitStrengthFP[d];
                        displayTeam = c.transitTeam[d];
                    }
                }
            }
            if ((flags & 1) != 0) {
                pxColor = getConfigSafe(displayTeam).colorRGBA;
                displayVal = sumStr >> FP_SHIFT;
            }
        }

        pixelBuffer[i] = pxColor;

        int base = i * 4;
        metaBuffer[base + 0] = static_cast<uint16_t>(c.terrain | ((c.flowDir & 3) << 4));
        metaBuffer[base + 1] = displayTeam;
        metaBuffer[base + 2] = flags;
        if (displayVal < 0) displayVal = 0;
        if (displayVal > 65535) displayVal = 65535;
        metaBuffer[base + 3] = static_cast<uint16_t>(displayVal);
    }

    void updateAllVisuals() {
        int total = width * height;
        for (int i = 0; i < total; ++i) {
            updateCellVisual(i);
        }
        ++renderVersion;
    }

    // Reconstruye la lista activa tras inicializaciones, ediciones masivas o saltos en el arbol. O(V).
    void rebuildActiveSet() {
        ++curActiveStamp;
        nextActiveCells.clear();
        int total = width * height;

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int idx = index(x, y);
                const Cell& c = currentGrid[idx];
                const TerrainProps& tp = TERRAIN_LUT[c.terrain];
                if (!tp.isHabitable) continue;

                if (!tp.isColonizable) {
                    if (c.hasAnyTransit()) markActiveNext(idx);
                    continue;
                }

                if (c.teamId == 0) continue;

                const TeamConfig& cfg = getConfigSafe(c.teamId);
                int32_t maxCap = static_cast<int32_t>((static_cast<int64_t>(cfg.maxCellTroopsFP) * tp.capMultFP) >> FP_SHIFT);
                if (c.localTroopsFP < maxCap || c.emissionCooldown > 0) {
                    markActiveNext(idx);
                    continue;
                }

                // Verificar si pertenece a la frontera
                for (int d = 0; d < 4; ++d) {
                    int nx = x + DX[d];
                    int ny = y + DY[d];
                    if (!inBounds(nx, ny)) continue;
                    const Cell& nb = currentGrid[index(nx, ny)];
                    if (TERRAIN_LUT[nb.terrain].isHabitable &&
                        (!TERRAIN_LUT[nb.terrain].isColonizable || nb.teamId != c.teamId)) {
                        markActiveNext(idx);
                        break;
                    }
                }
            }
        }
        Core::swapValues(activeCells, nextActiveCells);
    }

    /**
     * Fase 1: Identificacion de naciones via BFS y construccion de grafo territorial.
     * Solo se ejecuta cuando topologyDirty == true (al cambiar propiedad de alguna celda).
     * Complejidad temporal: O(V).
     */
    void computeTopologyAndTelemetry() {
        int totalCells = width * height;
        for (int i = 0; i < totalCells; ++i) {
            currentGrid[i].nationId = 0;
        }

        nationSizes.clear();
        nationTroopsFP.clear();
        nationTeams.clear();
        nationCentroidX.clear();
        nationCentroidY.clear();

        nationSizes.pushBack(0);
        nationTroopsFP.pushBack(0);
        nationTeams.pushBack(0);
        nationCentroidX.pushBack(0.0f);
        nationCentroidY.pushBack(0.0f);

        for (int t = 0; t < teamAreas.size(); ++t) {
            teamAreas[t] = 0;
            teamTroopsFP[t] = 0;
            teamNationCounts[t] = 0;
        }

        totalNations = 0;
        Core::CircularQueue<int> bfsQueue(totalCells > 64 ? totalCells / 4 : 64);

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int startIdx = index(x, y);
                Cell& startCell = currentGrid[startIdx];

                if (startCell.teamId == 0 || !TERRAIN_LUT[startCell.terrain].isColonizable || startCell.nationId != 0) {
                    continue;
                }

                ++totalNations;
                uint16_t team = startCell.teamId;
                int sizeCount = 0;
                int64_t troopSumFP = 0;
                int64_t sumX = 0;
                int64_t sumY = 0;

                startCell.nationId = totalNations;
                bfsQueue.enqueue(startIdx);

                while (!bfsQueue.isEmpty()) {
                    int currIdx = bfsQueue.dequeue();
                    const Cell& currCell = currentGrid[currIdx];
                    int cx = currIdx % width;
                    int cy = currIdx / width;

                    ++sizeCount;
                    troopSumFP += currCell.localTroopsFP;
                    sumX += cx;
                    sumY += cy;

                    for (int d = 0; d < 4; ++d) {
                        int nx = cx + DX[d];
                        int ny = cy + DY[d];
                        if (!inBounds(nx, ny)) continue;

                        int nIdx = index(nx, ny);
                        Cell& nb = currentGrid[nIdx];
                        if (TERRAIN_LUT[nb.terrain].isColonizable && nb.teamId == team && nb.nationId == 0) {
                            nb.nationId = totalNations;
                            bfsQueue.enqueue(nIdx);
                        }
                    }
                }

                float centX = (static_cast<float>(sumX) / static_cast<float>(sizeCount)) + 0.5f;
                float centY = (static_cast<float>(sumY) / static_cast<float>(sizeCount)) + 0.5f;

                nationSizes.pushBack(sizeCount);
                nationTroopsFP.pushBack(troopSumFP);
                nationTeams.pushBack(team);
                nationCentroidX.pushBack(centX);
                nationCentroidY.pushBack(centY);

                if (team < teamAreas.size()) {
                    teamAreas[team] += sizeCount;
                    teamTroopsFP[team] += troopSumFP;
                    teamNationCounts[team] += 1;
                }
            }
        }

        // Actualizacion de nodos y aristas del grafo territorial
        graphNodesBuf.clear();
        for (int n = 1; n <= totalNations; ++n) {
            graphNodesBuf.pushBack(nationCentroidX[n]);
            graphNodesBuf.pushBack(nationCentroidY[n]);
            graphNodesBuf.pushBack(static_cast<float>(nationTeams[n]));
            graphNodesBuf.pushBack(static_cast<float>(nationSizes[n]));
            graphNodesBuf.pushBack(static_cast<float>(nationTroopsFP[n]) / static_cast<float>(FP_ONE));
        }

        graphEdgesBuf.clear();
        Core::DynamicArray<NeighborList> adj;
        adj.resize(totalNations + 1, NeighborList());

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int nA = currentGrid[index(x, y)].nationId;
                if (nA <= 0) continue;

                if (x + 1 < width) {
                    int nB = currentGrid[index(x + 1, y)].nationId;
                    if (nB > 0 && nB != nA) {
                        int lo = nA < nB ? nA : nB;
                        int hi = nA < nB ? nB : nA;
                        if (!adj[lo].contains(hi)) {
                            adj[lo].add(hi);
                            graphEdgesBuf.pushBack(nationCentroidX[lo]);
                            graphEdgesBuf.pushBack(nationCentroidY[lo]);
                            graphEdgesBuf.pushBack(nationCentroidX[hi]);
                            graphEdgesBuf.pushBack(nationCentroidY[hi]);
                        }
                    }
                }
                if (y + 1 < height) {
                    int nB = currentGrid[index(x, y + 1)].nationId;
                    if (nB > 0 && nB != nA) {
                        int lo = nA < nB ? nA : nB;
                        int hi = nA < nB ? nB : nA;
                        if (!adj[lo].contains(hi)) {
                            adj[lo].add(hi);
                            graphEdgesBuf.pushBack(nationCentroidX[lo]);
                            graphEdgesBuf.pushBack(nationCentroidY[lo]);
                            graphEdgesBuf.pushBack(nationCentroidX[hi]);
                            graphEdgesBuf.pushBack(nationCentroidY[hi]);
                        }
                    }
                }
            }
        }
        topologyDirty = false;
    }

    // Resolucion de un grupo de intenciones con Max-Heap en enteros de 64 bits. Complejidad: O(1) (k <= 16).
    bool resolveGroupWithHeap(const Impulse* items, int count, TeamForce& outWinner, int64_t& outSecondForceFP) {
        if (count <= 0) return false;

        uint16_t uniqueTeams[16];
        int64_t teamSums[16];
        uint8_t teamDirs[16];
        uint8_t teamFromTransit[16];
        int uniqueCount = 0;

        for (int i = 0; i < count; ++i) {
            uint16_t t = items[i].teamId;
            if (t == 0) continue;
            int found = -1;
            for (int u = 0; u < uniqueCount; ++u) {
                if (uniqueTeams[u] == t) {
                    found = u;
                    break;
                }
            }
            if (found >= 0) {
                teamSums[found] += items[i].strengthFP;
            } else if (uniqueCount < 16) {
                uniqueTeams[uniqueCount] = t;
                teamSums[uniqueCount] = items[i].strengthFP;
                teamDirs[uniqueCount] = items[i].moveDir;
                teamFromTransit[uniqueCount] = items[i].fromTransit;
                ++uniqueCount;
            }
        }

        if (uniqueCount == 0) return false;

        Core::PriorityQueue<TeamForce> pq;
        for (int u = 0; u < uniqueCount; ++u) {
            pq.push(TeamForce(uniqueTeams[u], teamSums[u], teamDirs[u], teamFromTransit[u]));
        }

        outWinner = pq.pop();
        outSecondForceFP = pq.isEmpty() ? 0 : pq.pop().totalStrengthFP;
        return true;
    }

    /**
     * Ejecuta un tick determinista procesando unicamente las celdas activas y las intenciones tocadas.
     * Complejidad temporal: O(|activeCells| + |touchedIntents|) en ticks sin cambio de frontera;
     * O(V) si hay cambios de propiedad que requieren actualizar componentes conexas.
     */
    void executeSingleTick(bool recordUndo) {
        if (topologyDirty) {
            computeTopologyAndTelemetry();
        }

        ++curDirtyStamp;
        dirtyIndices.clear();
        ++curIntentStamp;
        touchedIntents.clear();
        ++curActiveStamp;
        nextActiveCells.clear();

        // 1. Procesar ordenes manuales del jugador desde la Cola Circular
        while (!playerOrderQueue.isEmpty()) {
            PlayerOrder po = playerOrderQueue.dequeue();
            if (!inBounds(po.x, po.y)) continue;
            int oIdx = index(po.x, po.y);
            const Cell& origin = currentGrid[oIdx];
            if (origin.teamId == 0 || !TERRAIN_LUT[origin.terrain].isColonizable || origin.nationId <= 0) continue;

            int nx = po.x + DX[po.dir];
            int ny = po.y + DY[po.dir];
            if (!inBounds(nx, ny) || !TERRAIN_LUT[currentGrid[index(nx, ny)].terrain].isHabitable) continue;

            int64_t force64 = (nationTroopsFP[origin.nationId] * po.fractionFP) >> FP_SHIFT;
            int32_t forceFP = force64 > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<int32_t>(force64);
            if (forceFP > 0) {
                addIntent(nx, ny, Impulse(origin.teamId, forceFP, static_cast<uint8_t>(po.dir), 0));
                markActiveNext(oIdx);
            }
        }

        // 2. Evaluar unicamente las celdas activas (transito, fronteras, enfriamiento o regeneracion)
        for (int a = 0; a < activeCells.size(); ++a) {
            int idx = activeCells[a];
            const Cell& c = currentGrid[idx];
            const TerrainProps& tp = TERRAIN_LUT[c.terrain];
            if (!tp.isHabitable) continue;

            int x = idx % width;
            int y = idx / width;

            // --- Caso A: Celda de transito (T o W) ---
            if (!tp.isColonizable) {
                if (!c.hasAnyTransit()) continue;
                ensureDirty(idx).clearTransit();

                if (c.terrain == TERRAIN_W) {
                    // Corriente / Diodo: toda senal sale forzada en c.flowDir
                    uint8_t outDir = c.flowDir & 3;
                    int nx = x + DX[outDir];
                    int ny = y + DY[outDir];
                    if (inBounds(nx, ny) && TERRAIN_LUT[currentGrid[index(nx, ny)].terrain].isHabitable) {
                        for (int d = 0; d < 4; ++d) {
                            if (c.transitTeam[d] != 0) {
                                addIntent(nx, ny, Impulse(c.transitTeam[d], c.transitStrengthFP[d], outDir, 1));
                            }
                        }
                    }
                } else {
                    for (int d = 0; d < 4; ++d) {
                        if (c.transitTeam[d] == 0) continue;
                        const TeamConfig& cfg = getConfigSafe(c.transitTeam[d]);
                        if (cfg.multiDirTransit) {
                            int backDir = oppositeDirection(d);
                            for (int outDir = 0; outDir < 4; ++outDir) {
                                if (outDir == backDir) continue;
                                int nx = x + DX[outDir];
                                int ny = y + DY[outDir];
                                if (inBounds(nx, ny) && TERRAIN_LUT[currentGrid[index(nx, ny)].terrain].isHabitable) {
                                    addIntent(nx, ny, Impulse(c.transitTeam[d], c.transitStrengthFP[d], static_cast<uint8_t>(outDir), 1));
                                }
                            }
                        } else {
                            int nx = x + DX[d];
                            int ny = y + DY[d];
                            if (inBounds(nx, ny) && TERRAIN_LUT[currentGrid[index(nx, ny)].terrain].isHabitable) {
                                addIntent(nx, ny, Impulse(c.transitTeam[d], c.transitStrengthFP[d], static_cast<uint8_t>(d), 1));
                            }
                        }
                    }
                }
                continue;
            }

            // --- Caso B: Celda colonizable permanente ---
            if (c.teamId == 0 || c.nationId <= 0) continue;

            const TeamConfig& cfg = getConfigSafe(c.teamId);
            bool stayActive = false;

            // Regeneracion entera con multiplicador de terreno y growthFactor individual (base 64)
            int32_t maxCap = static_cast<int32_t>((static_cast<int64_t>(cfg.maxCellTroopsFP) * tp.capMultFP) >> FP_SHIFT);
            if (maxCap < FP_ONE) maxCap = FP_ONE;

            if (cfg.troopRegenFP > 0 && c.localTroopsFP < maxCap) {
                int64_t delta = (static_cast<int64_t>(cfg.troopRegenFP) * tp.regenMultFP) >> FP_SHIFT;
                delta = (delta * c.growthFactor) >> GROWTH_SHIFT;
                if (delta < 1) delta = 1;
                int32_t newTroops = c.localTroopsFP + static_cast<int32_t>(delta);
                if (newTroops > maxCap) newTroops = maxCap;
                if (newTroops != c.localTroopsFP) {
                    ensureDirty(idx).localTroopsFP = newTroops;
                }
                if (newTroops < maxCap) stayActive = true;
            }

            // Decremento de enfriamiento de emision
            if (c.emissionCooldown > 0) {
                int16_t nextCd = static_cast<int16_t>(c.emissionCooldown - 1);
                ensureDirty(idx).emissionCooldown = nextCd;
                if (nextCd > 0) stayActive = true;
            }

            int nSize = nationSizes[c.nationId];
            int64_t myForce64 = nationTroopsFP[c.nationId];
            int32_t myForceFP = myForce64 > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<int32_t>(myForce64);

            bool atAreaLimit = (cfg.maxAreaLimit > 0 && nSize >= cfg.maxAreaLimit);
            bool canEmitTransit = false;
            if (cfg.emissionPeriod < 0) {
                canEmitTransit = ((c.flags & 1) == 0 && nSize >= 2);
            } else if (cfg.emissionPeriod > 0) {
                canEmitTransit = (c.emissionCooldown == 0);
            }

            // Velocidad de expansion entera: v(N) = FP_ONE^2 / (FP_ONE + beta * S(N))
            int64_t denom = static_cast<int64_t>(FP_ONE) + static_cast<int64_t>(cfg.expansionFrictionFP) * nSize;
            int32_t speedFP = static_cast<int32_t>((static_cast<int64_t>(FP_ONE) * FP_ONE) / (denom > 0 ? denom : 1));
            if (speedFP < 1) speedFP = 1;

            // Determinar si hay vecinos accionables (frontera) y elegir objetivo
            bool hasActionableNeighbor = false;
            int bestDir = -1;
            int32_t bestCostFP = 0x7FFFFFFF;
            bool bestIsTransit = false;

            for (int k = 0; k < 4; ++k) {
                int d = cfg.priorityOrder[k];
                int nx = x + DX[d];
                int ny = y + DY[d];
                if (!inBounds(nx, ny)) continue;

                const Cell& target = currentGrid[index(nx, ny)];
                const TerrainProps& ttp = TERRAIN_LUT[target.terrain];
                if (!ttp.isHabitable) continue;

                if (!ttp.isColonizable) {
                    hasActionableNeighbor = true;
                    if (canEmitTransit) {
                        if (!cfg.organicExpansion || bestDir == -1) {
                            bestDir = d;
                            bestIsTransit = true;
                            bestCostFP = 0;
                            break;
                        }
                    }
                    continue;
                }

                if (target.teamId == c.teamId) continue;
                hasActionableNeighbor = true;
                if (atAreaLimit) continue;

                // Evaluar si la conquista es viable contra enemigo (incluyendo multiplicador de Bastion F)
                if (target.teamId != 0) {
                    int64_t enemyForce64 = (target.nationId > 0) ? nationTroopsFP[target.nationId] : 0;
                    int64_t effTauFP = (static_cast<int64_t>(cfg.conquestThresholdFP) * ttp.defMultFP) >> FP_SHIFT;
                    if (myForce64 * FP_ONE < (FP_ONE + effTauFP) * enemyForce64) {
                        continue;
                    }
                }

                if (!cfg.organicExpansion) {
                    // Modo determinista estricto (compuertas logicas)
                    bestDir = d;
                    bestIsTransit = false;
                    bestCostFP = FP_ONE;
                    break;
                } else {
                    // Modo organico determinista: costo por terreno + growthFactor + cohesion de frontera
                    int32_t gf = target.growthFactor > 0 ? target.growthFactor : 1;
                    int32_t cost = (static_cast<int32_t>(ttp.costFP) * GROWTH_BASE) / gf;

                    // Contar vecinos ortogonales de la celda destino que ya pertenecen al mismo equipo
                    int alliedAdj = 0;
                    for (int nd = 0; nd < 4; ++nd) {
                        int nnx = nx + DX[nd];
                        int nny = ny + DY[nd];
                        if (inBounds(nnx, nny)) {
                            const Cell& nnb = currentGrid[index(nnx, nny)];
                            if (TERRAIN_LUT[nnb.terrain].isColonizable && nnb.teamId == c.teamId) {
                                ++alliedAdj;
                            }
                        }
                    }
                    if (alliedAdj >= 3) cost = (cost * 512) >> FP_SHIFT;
                    else if (alliedAdj == 2) cost = (cost * 768) >> FP_SHIFT;

                    // Sesgo leve por orden de prioridad para desempate determinista en terreno plano
                    cost += k * 48;
                    if (cost < 128) cost = 128;

                    if (cost < bestCostFP) {
                        bestCostFP = cost;
                        bestDir = d;
                        bestIsTransit = false;
                    }
                }
            }

            if (hasActionableNeighbor) {
                stayActive = true;
            }

            if (bestDir != -1) {
                int nx = x + DX[bestDir];
                int ny = y + DY[bestDir];
                if (bestIsTransit) {
                    addIntent(nx, ny, Impulse(c.teamId, myForceFP, static_cast<uint8_t>(bestDir), 0));
                    Cell& dCell = ensureDirty(idx);
                    if (cfg.emissionPeriod < 0) {
                        dCell.flags |= 1;
                    } else {
                        dCell.emissionCooldown = static_cast<int16_t>(cfg.emissionPeriod);
                    }
                } else {
                    int32_t accum = static_cast<int32_t>(c.expansionAccumFP) + speedFP;
                    if (accum >= bestCostFP) {
                        accum -= bestCostFP;
                        if (accum > 65535) accum = 65535;
                        addIntent(nx, ny, Impulse(c.teamId, myForceFP, static_cast<uint8_t>(bestDir), 0));
                    }
                    if (accum > 65535) accum = 65535;
                    if (static_cast<uint16_t>(accum) != c.expansionAccumFP) {
                        ensureDirty(idx).expansionAccumFP = static_cast<uint16_t>(accum);
                    }
                }
            }

            if (stayActive) {
                markActiveNext(idx);
            }
        }

        // 3. Resolucion de colisiones unicamente en las celdas que recibieron intenciones
        for (int ti = 0; ti < touchedIntents.size(); ++ti) {
            int i = touchedIntents[ti];
            const CellIntentBuffer& buf = intentGrid[i];
            if (buf.count == 0) continue;

            const Cell& targetCurr = currentGrid[i];
            const TerrainProps& ttp = TERRAIN_LUT[targetCurr.terrain];

            if (!ttp.isColonizable) {
                // Celda de transito (T o W)
                bool anyMultiDir = false;
                for (int j = 0; j < buf.count; ++j) {
                    if (getConfigSafe(buf.list[j].teamId).multiDirTransit) {
                        anyMultiDir = true;
                        break;
                    }
                }

                if (anyMultiDir) {
                    TeamForce winner;
                    int64_t secondForceFP = 0;
                    if (resolveGroupWithHeap(buf.list, buf.count, winner, secondForceFP)) {
                        int32_t tauFP = getConfigSafe(winner.teamId).conquestThresholdFP;
                        if (winner.totalStrengthFP * FP_ONE >= (FP_ONE + tauFP) * secondForceFP) {
                            Cell& dCell = ensureDirty(i);
                            dCell.transitTeam[winner.primaryDir] = winner.teamId;
                            dCell.transitStrengthFP[winner.primaryDir] =
                                winner.totalStrengthFP > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<int32_t>(winner.totalStrengthFP);
                        }
                    }
                } else {
                    int axisDirs[2][2] = { { DIR_UP, DIR_DOWN }, { DIR_LEFT, DIR_RIGHT } };
                    for (int axis = 0; axis < 2; ++axis) {
                        Impulse axisBuf[16];
                        int axisCount = 0;
                        uint16_t firstTeam = 0;
                        bool multipleTeams = false;

                        for (int j = 0; j < buf.count; ++j) {
                            int md = buf.list[j].moveDir;
                            if (md == axisDirs[axis][0] || md == axisDirs[axis][1]) {
                                axisBuf[axisCount++] = buf.list[j];
                                if (firstTeam == 0) firstTeam = buf.list[j].teamId;
                                else if (buf.list[j].teamId != firstTeam) multipleTeams = true;
                            }
                        }

                        if (axisCount == 0) continue;

                        if (!multipleTeams) {
                            Cell& dCell = ensureDirty(i);
                            for (int j = 0; j < axisCount; ++j) {
                                uint8_t d = axisBuf[j].moveDir;
                                dCell.transitTeam[d] = axisBuf[j].teamId;
                                int64_t sum = static_cast<int64_t>(dCell.transitStrengthFP[d]) + axisBuf[j].strengthFP;
                                dCell.transitStrengthFP[d] = sum > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<int32_t>(sum);
                            }
                        } else {
                            TeamForce winner;
                            int64_t secondForceFP = 0;
                            if (resolveGroupWithHeap(axisBuf, axisCount, winner, secondForceFP)) {
                                int32_t tauFP = getConfigSafe(winner.teamId).conquestThresholdFP;
                                if (winner.totalStrengthFP * FP_ONE >= (FP_ONE + tauFP) * secondForceFP) {
                                    Cell& dCell = ensureDirty(i);
                                    dCell.transitTeam[winner.primaryDir] = winner.teamId;
                                    dCell.transitStrengthFP[winner.primaryDir] =
                                        winner.totalStrengthFP > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<int32_t>(winner.totalStrengthFP);
                                }
                            }
                        }
                    }
                }
            } else {
                // Celda colonizable permanente (H, O, R, F, S)
                TeamForce winner;
                int64_t secondForceFP = 0;
                if (!resolveGroupWithHeap(buf.list, buf.count, winner, secondForceFP)) continue;

                const TeamConfig& winCfg = getConfigSafe(winner.teamId);
                int64_t effTauFP = (static_cast<int64_t>(winCfg.conquestThresholdFP) * ttp.defMultFP) >> FP_SHIFT;

                if (targetCurr.teamId == 0) {
                    if (winner.totalStrengthFP * FP_ONE >= (FP_ONE + winCfg.conquestThresholdFP) * secondForceFP) {
                        Cell& dCell = ensureDirty(i);
                        dCell.teamId = winner.teamId;
                        dCell.localTroopsFP = FP_ONE;
                        dCell.expansionAccumFP = 0;
                        dCell.emissionCooldown = 0;
                        dCell.flags = (winCfg.emissionPeriod < 0 && winner.fromTransit) ? 1 : 0;
                    }
                } else {
                    uint16_t defTeam = targetCurr.teamId;
                    int64_t defForce64 = (targetCurr.nationId > 0) ? nationTroopsFP[targetCurr.nationId] : 0;

                    if (winner.teamId != defTeam &&
                        winner.totalStrengthFP * FP_ONE >= (FP_ONE + effTauFP) * defForce64 &&
                        winner.totalStrengthFP * FP_ONE >= (FP_ONE + winCfg.conquestThresholdFP) * secondForceFP) {
                        Cell& dCell = ensureDirty(i);
                        dCell.teamId = winner.teamId;
                        dCell.localTroopsFP = FP_ONE;
                        dCell.expansionAccumFP = 0;
                        dCell.emissionCooldown = 0;
                        dCell.flags = 0;
                    }
                }
            }
        }

        // 4. Commit diferencial unicamente sobre dirtyIndices
        for (int di = 0; di < dirtyIndices.size(); ++di) {
            int idx = dirtyIndices[di];
            const Cell& oldC = currentGrid[idx];
            const Cell& newC = nextGrid[idx];

            if (oldC == newC) continue;

            if (recordUndo) {
                historyTree.recordChange(idx, oldC, newC);
            }

            if (oldC.teamId != newC.teamId) {
                topologyDirty = true;
                markCellAndNeighborsActive(idx);
            } else {
                // Actualizacion incremental de tropas sin re-ejecutar BFS
                if (!topologyDirty && newC.teamId > 0 && oldC.nationId > 0 && newC.localTroopsFP != oldC.localTroopsFP) {
                    int64_t diff = static_cast<int64_t>(newC.localTroopsFP) - static_cast<int64_t>(oldC.localTroopsFP);
                    nationTroopsFP[oldC.nationId] += diff;
                    if (newC.teamId < teamTroopsFP.size()) {
                        teamTroopsFP[newC.teamId] += diff;
                    }
                }
                if (newC.hasAnyTransit()) {
                    markActiveNext(idx);
                }
            }

            currentGrid[idx] = newC;
            updateCellVisual(idx);
        }

        Core::swapValues(activeCells, nextActiveCells);
        ++currentTick;
        ++renderVersion;
    }

    void resetDefaultTeams(bool logicMode) {
        teamConfigs.clear();
        teamAreas.clear();
        teamTroopsFP.clear();
        teamNationCounts.clear();

        teamConfigs.pushBack(TeamConfig());
        teamAreas.pushBack(0);
        teamTroopsFP.pushBack(0);
        teamNationCounts.pushBack(0);

        for (uint16_t t = 1; t <= 3; ++t) {
            ensureTeamExists(t);
            TeamConfig& cfg = teamConfigs[t];
            cfg.conquestThresholdFP = 128; // 12.5% exacto
            if (logicMode) {
                cfg.emissionPeriod = -1;
                cfg.maxAreaLimit = 9;
                cfg.expansionFrictionFP = 0;
                cfg.multiDirTransit = true;
                cfg.organicExpansion = false;
                cfg.troopRegenFP = 0;
                cfg.maxCellTroopsFP = FP_ONE;
            } else {
                cfg.emissionPeriod = 8;
                cfg.maxAreaLimit = 0;
                cfg.expansionFrictionFP = 4;
                cfg.multiDirTransit = false;
                cfg.organicExpansion = true;
                cfg.troopRegenFP = 256;
                cfg.maxCellTroopsFP = 10240;
            }
        }

        if (!logicMode) {
            teamConfigs[2].priorityOrder[0] = DIR_RIGHT;
            teamConfigs[2].priorityOrder[1] = DIR_DOWN;
            teamConfigs[2].priorityOrder[2] = DIR_LEFT;
            teamConfigs[2].priorityOrder[3] = DIR_UP;
        }
    }

public:
    SimulationWorld()
        : width(0), height(0), currentTick(0), totalNations(0),
          renderVersion(1), topologyDirty(true),
          curActiveStamp(1), curDirtyStamp(1), curIntentStamp(1),
          playerOrderQueue(64) {
        resetDefaultTeams(false);
    }

    void initGrid(int w, int h, bool logicMode) {
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        width = w;
        height = h;
        currentTick = 0;
        totalNations = 0;
        topologyDirty = true;

        resetDefaultTeams(logicMode);

        int total = width * height;
        currentGrid.resize(total, Cell());
        nextGrid.resize(total, Cell());
        intentGrid.resize(total, CellIntentBuffer());

        activeStamp.resize(total, 0); activeStamp.fill(0); curActiveStamp = 1;
        dirtyStamp.resize(total, 0);  dirtyStamp.fill(0);  curDirtyStamp = 1;
        intentStamp.resize(total, 0); intentStamp.fill(0); curIntentStamp = 1;

        pixelBuffer.resize(total, 0);
        metaBuffer.resize(total * 4, 0);

        playerOrderQueue.clear();
        historyTree.reset(total, 0);
    }

    void initBlankMap(int w, int h) {
        initGrid(w, h, false);
        int total = width * height;
        for (int i = 0; i < total; ++i) {
            currentGrid[i] = Cell();
            currentGrid[i].terrain = TERRAIN_N;
        }
        computeTopologyAndTelemetry();
        rebuildActiveSet();
        updateAllVisuals();
    }

    void loadSandbox(int w, int h) {
        initGrid(w, h, false);

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                Cell& c = currentGrid[index(x, y)];
                c = Cell();
                // Gradiente organico determinista de fertilidad usando hash entero suave
                int wave = ((x * 7 + y * 13) ^ (x * 3 - y * 5)) & 31;
                c.growthFactor = static_cast<uint8_t>(48 + wave); // Rango 48..79 alrededor de 64

                if (x >= width / 2 - 2 && x <= width / 2 + 2) {
                    if ((y >= 10 && y <= 14) || (y >= height - 15 && y <= height - 11)) {
                        c.terrain = TERRAIN_T;
                    } else {
                        c.terrain = TERRAIN_N;
                    }
                } else if ((x - width / 4) * (x - width / 4) + (y - height / 2) * (y - height / 2) <= 25) {
                    c.terrain = TERRAIN_R; // Valle fertil
                    c.growthFactor = 112;
                } else if ((x - (3 * width) / 4) * (x - (3 * width) / 4) + (y - height / 2) * (y - height / 2) <= 20) {
                    c.terrain = TERRAIN_F; // Zona fortificada
                }
            }
        }

        for (int dy = 0; dy < 3; ++dy) {
            for (int dx = 0; dx < 4; ++dx) {
                int i1 = index(6 + dx, 11 + dy);
                currentGrid[i1].terrain = TERRAIN_H;
                currentGrid[i1].teamId = 1;
                currentGrid[i1].localTroopsFP = FP_ONE;

                int i2 = index(width - 10 + dx, 11 + dy);
                currentGrid[i2].terrain = TERRAIN_H;
                currentGrid[i2].teamId = 2;
                currentGrid[i2].localTroopsFP = FP_ONE;

                int i3 = index(8 + dx, height - 14 + dy);
                currentGrid[i3].terrain = TERRAIN_H;
                currentGrid[i3].teamId = 3;
                currentGrid[i3].localTroopsFP = FP_ONE;
            }
        }

        computeTopologyAndTelemetry();
        rebuildActiveSet();
        updateAllVisuals();
    }

    void loadXorGate(bool enableA, bool enableB) {
        initGrid(9, 9, true);

        const char* layout[9] = {
            "AHHNNNNNN",
            "AHHTTNNNN",
            "AHHNTNNNN",
            "NNNNTNNNN",
            "NNNHTTTON",
            "NNNNTNNNN",
            "BHHNTNNNN",
            "BHHTTNNNN",
            "BHHNNNNNN"
        };

        for (int y = 0; y < 9; ++y) {
            for (int x = 0; x < 9; ++x) {
                char ch = layout[y][x];
                Cell& c = currentGrid[index(x, y)];
                c = Cell();
                c.growthFactor = GROWTH_BASE;

                if (ch == 'N')      c.terrain = TERRAIN_N;
                else if (ch == 'T') c.terrain = TERRAIN_T;
                else if (ch == 'H') c.terrain = TERRAIN_H;
                else if (ch == 'O') c.terrain = TERRAIN_O;
                else if (ch == 'A') {
                    c.terrain = TERRAIN_H;
                    c.teamId = enableA ? 1 : 0;
                    c.localTroopsFP = enableA ? FP_ONE : 0;
                } else if (ch == 'B') {
                    c.terrain = TERRAIN_H;
                    c.teamId = enableB ? 2 : 0;
                    c.localTroopsFP = enableB ? FP_ONE : 0;
                }
            }
        }

        computeTopologyAndTelemetry();
        rebuildActiveSet();
        updateAllVisuals();
    }

    void stepBatch(int steps, bool recordUndo) {
        if (steps < 1) return;
        if (recordUndo) {
            historyTree.beginTransaction(1);
        }
        for (int s = 0; s < steps; ++s) {
            executeSingleTick(recordUndo);
        }
        if (topologyDirty) {
            computeTopologyAndTelemetry();
        }
        if (recordUndo) {
            historyTree.commitTransaction(currentTick);
        }
    }

    // =========================================================================
    // Transacciones Atomicas y Navegacion del Arbol N-ario de Historial
    // =========================================================================

    void beginEditTransaction() {
        historyTree.beginTransaction(2);
    }

    void commitEditTransaction() {
        historyTree.commitTransaction(currentTick);
        topologyDirty = true;
        computeTopologyAndTelemetry();
        rebuildActiveSet();
        updateAllVisuals();
    }

    bool undo() {
        if (!historyTree.undo(currentGrid, currentTick)) return false;
        topologyDirty = true;
        computeTopologyAndTelemetry();
        rebuildActiveSet();
        updateAllVisuals();
        return true;
    }

    bool redo() {
        if (!historyTree.redo(currentGrid, currentTick)) return false;
        topologyDirty = true;
        computeTopologyAndTelemetry();
        rebuildActiveSet();
        updateAllVisuals();
        return true;
    }

    bool jumpToHistoryNode(int nodeId) {
        if (!historyTree.jumpToNode(nodeId, currentGrid, currentTick)) return false;
        topologyDirty = true;
        computeTopologyAndTelemetry();
        rebuildActiveSet();
        updateAllVisuals();
        return true;
    }

    // =========================================================================
    // Herramientas de Edicion Diferida, Flood Fill y Primitivas Geometricas
    // =========================================================================

    void paintCellRaw(int x, int y, int mode, int teamId, int auxParam) {
        if (!inBounds(x, y)) return;
        int idx = index(x, y);
        Cell before = currentGrid[idx];
        Cell& c = currentGrid[idx];

        if (mode == 10) {
            // Pincel de multiplicador individual growthFactor (16..255)
            int gf = auxParam < 16 ? 16 : (auxParam > 255 ? 255 : auxParam);
            c.growthFactor = static_cast<uint8_t>(gf);
        } else {
            c.clearTransit();
            c.flags = 0;
            c.emissionCooldown = 0;
            c.expansionAccumFP = 0;

            if (mode == 0) { c.terrain = TERRAIN_N; c.teamId = 0; c.localTroopsFP = 0; }
            else if (mode == 1) { c.terrain = TERRAIN_H; c.teamId = 0; c.localTroopsFP = 0; }
            else if (mode == 2) { c.terrain = TERRAIN_T; c.teamId = 0; c.localTroopsFP = 0; }
            else if (mode == 3) {
                ensureTeamExists(static_cast<uint16_t>(teamId));
                if (!TERRAIN_LUT[c.terrain].isColonizable) c.terrain = TERRAIN_H;
                c.teamId = static_cast<uint16_t>(teamId);
                c.localTroopsFP = FP_ONE;
            }
            else if (mode == 4) { c.terrain = TERRAIN_O; c.teamId = 0; c.localTroopsFP = 0; }
            else if (mode == 5) { c.teamId = 0; c.localTroopsFP = 0; }
            else if (mode == 6) { c.terrain = TERRAIN_R; c.teamId = 0; c.localTroopsFP = 0; }
            else if (mode == 7) { c.terrain = TERRAIN_F; c.teamId = 0; c.localTroopsFP = 0; }
            else if (mode == 8) { c.terrain = TERRAIN_S; c.teamId = 0; c.localTroopsFP = 0; }
            else if (mode == 9) {
                c.terrain = TERRAIN_W;
                c.flowDir = static_cast<uint8_t>(auxParam & 3);
                c.teamId = 0;
                c.localTroopsFP = 0;
            }
        }

        if (historyTree.isInTransaction()) {
            historyTree.recordChange(idx, before, c);
        }
        updateCellVisual(idx);
        ++renderVersion;
    }

    void setCellExact(int x, int y, int terrainCode, int teamId, int troopsFP, int growthFactor, int flowDir) {
        if (!inBounds(x, y)) return;
        int idx = index(x, y);
        Cell before = currentGrid[idx];
        Cell& c = currentGrid[idx];
        c = Cell();
        c.terrain = static_cast<uint8_t>(terrainCode & 7);
        c.growthFactor = static_cast<uint8_t>(growthFactor < 16 ? 16 : (growthFactor > 255 ? 255 : growthFactor));
        c.flowDir = static_cast<uint8_t>(flowDir & 3);

        if (teamId > 0 && TERRAIN_LUT[c.terrain].isColonizable) {
            ensureTeamExists(static_cast<uint16_t>(teamId));
            c.teamId = static_cast<uint16_t>(teamId);
            c.localTroopsFP = troopsFP > 0 ? troopsFP : FP_ONE;
        }

        if (historyTree.isInTransaction()) {
            historyTree.recordChange(idx, before, c);
        }
        updateCellVisual(idx);
        ++renderVersion;
    }

    void eraseNationAt(int x, int y) {
        if (!inBounds(x, y)) return;
        int startIdx = index(x, y);
        uint16_t targetTeam = currentGrid[startIdx].teamId;
        if (targetTeam == 0 || !TERRAIN_LUT[currentGrid[startIdx].terrain].isColonizable) return;

        Core::CircularQueue<int> q(64);
        Cell before = currentGrid[startIdx];
        currentGrid[startIdx].teamId = 0;
        currentGrid[startIdx].localTroopsFP = 0;
        if (historyTree.isInTransaction()) historyTree.recordChange(startIdx, before, currentGrid[startIdx]);
        q.enqueue(startIdx);

        while (!q.isEmpty()) {
            int curr = q.dequeue();
            int cx = curr % width;
            int cy = curr / width;
            for (int d = 0; d < 4; ++d) {
                int nx = cx + DX[d];
                int ny = cy + DY[d];
                if (!inBounds(nx, ny)) continue;
                int nIdx = index(nx, ny);
                if (TERRAIN_LUT[currentGrid[nIdx].terrain].isColonizable && currentGrid[nIdx].teamId == targetTeam) {
                    Cell nbBefore = currentGrid[nIdx];
                    currentGrid[nIdx].teamId = 0;
                    currentGrid[nIdx].localTroopsFP = 0;
                    if (historyTree.isInTransaction()) historyTree.recordChange(nIdx, nbBefore, currentGrid[nIdx]);
                    q.enqueue(nIdx);
                }
            }
        }
    }

    void floodFill(int startX, int startY, int mode, int teamId, int auxParam) {
        if (!inBounds(startX, startY)) return;
        int startIdx = index(startX, startY);
        Cell ref = currentGrid[startIdx];

        Core::DynamicArray<uint8_t> visited;
        visited.resize(width * height, 0);
        Core::CircularQueue<int> q(128);

        visited[startIdx] = 1;
        q.enqueue(startIdx);

        while (!q.isEmpty()) {
            int curr = q.dequeue();
            int cx = curr % width;
            int cy = curr / width;

            paintCellRaw(cx, cy, mode, teamId, auxParam);

            for (int d = 0; d < 4; ++d) {
                int nx = cx + DX[d];
                int ny = cy + DY[d];
                if (!inBounds(nx, ny)) continue;
                int nIdx = index(nx, ny);
                if (visited[nIdx]) continue;

                const Cell& nb = currentGrid[nIdx];
                if (nb.terrain == ref.terrain && nb.teamId == ref.teamId) {
                    visited[nIdx] = 1;
                    q.enqueue(nIdx);
                }
            }
        }
    }

    void paintLine(int x0, int y0, int x1, int y1, int mode, int teamId, int auxParam) {
        int dx = absInt(x1 - x0);
        int dy = -absInt(y1 - y0);
        int sx = x0 < x1 ? 1 : -1;
        int sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;

        while (true) {
            paintCellRaw(x0, y0, mode, teamId, auxParam);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    void paintRect(int x0, int y0, int x1, int y1, int mode, int teamId, int auxParam, bool filled) {
        int minX = x0 < x1 ? x0 : x1;
        int maxX = x0 > x1 ? x0 : x1;
        int minY = y0 < y1 ? y0 : y1;
        int maxY = y0 > y1 ? y0 : y1;

        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                if (filled || x == minX || x == maxX || y == minY || y == maxY) {
                    paintCellRaw(x, y, mode, teamId, auxParam);
                }
            }
        }
    }

    void paintCircle(int cx, int cy, int radius, int mode, int teamId, int auxParam, bool filled) {
        if (radius < 0) radius = 0;
        int r2 = radius * radius;
        int innerR = radius > 0 ? (radius - 1) : 0;
        int innerR2 = innerR * innerR;

        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                int dist2 = dx * dx + dy * dy;
                if (dist2 <= r2 && (filled || radius == 0 || dist2 >= innerR2)) {
                    paintCellRaw(cx + dx, cy + dy, mode, teamId, auxParam);
                }
            }
        }
    }

    void enqueueTroopDispatch(int x, int y, int dir, float fraction) {
        if (dir < 0 || dir > 3) return;
        int32_t fFP = static_cast<int32_t>(fraction * static_cast<float>(FP_ONE));
        if (fFP < 64) fFP = 64;
        if (fFP > FP_ONE) fFP = FP_ONE;
        playerOrderQueue.enqueue(PlayerOrder(x, y, dir, fFP));
    }

    // =========================================================================
    // Gestion Dinamica de Equipos y Doctrinas
    // =========================================================================

    int createTeam() {
        int newId = teamConfigs.size();
        ensureTeamExists(static_cast<uint16_t>(newId));
        return newId;
    }

    void deleteTeam(int teamId) {
        if (teamId <= 0 || teamId >= teamConfigs.size()) return;
        historyTree.beginTransaction(2);
        teamConfigs[teamId].active = false;
        int total = width * height;
        for (int i = 0; i < total; ++i) {
            Cell before = currentGrid[i];
            bool changed = false;
            if (currentGrid[i].teamId == static_cast<uint16_t>(teamId)) {
                currentGrid[i].teamId = 0;
                currentGrid[i].localTroopsFP = 0;
                changed = true;
            }
            for (int d = 0; d < 4; ++d) {
                if (currentGrid[i].transitTeam[d] == static_cast<uint16_t>(teamId)) {
                    currentGrid[i].transitTeam[d] = 0;
                    currentGrid[i].transitStrengthFP[d] = 0;
                    changed = true;
                }
            }
            if (changed) historyTree.recordChange(i, before, currentGrid[i]);
        }
        commitEditTransaction();
    }

    void setTeamColor(int teamId, int r, int g, int b) {
        if (teamId <= 0) return;
        ensureTeamExists(static_cast<uint16_t>(teamId));
        teamConfigs[teamId].colorRGBA = packRGBA(
            static_cast<uint8_t>(r & 255),
            static_cast<uint8_t>(g & 255),
            static_cast<uint8_t>(b & 255)
        );
        updateAllVisuals();
    }

    void setTeamConfig(int teamId, int d0, int d1, int d2, int d3,
                       float tau, int emissionPeriod, int maxArea,
                       float friction, bool multiDir, float regenRate,
                       float maxTroops, bool organic) {
        if (teamId <= 0) return;
        ensureTeamExists(static_cast<uint16_t>(teamId));
        TeamConfig& cfg = teamConfigs[teamId];
        cfg.active = true;
        cfg.priorityOrder[0] = d0 & 3;
        cfg.priorityOrder[1] = d1 & 3;
        cfg.priorityOrder[2] = d2 & 3;
        cfg.priorityOrder[3] = d3 & 3;
        cfg.conquestThresholdFP = tau < 0.0f ? 0 : static_cast<int32_t>(tau * FP_ONE + 0.5f);
        cfg.emissionPeriod = emissionPeriod;
        cfg.maxAreaLimit = maxArea < 0 ? 0 : maxArea;
        cfg.expansionFrictionFP = friction < 0.0f ? 0 : static_cast<int32_t>(friction * FP_ONE + 0.5f);
        cfg.multiDirTransit = multiDir;
        cfg.troopRegenFP = regenRate < 0.0f ? 0 : static_cast<int32_t>(regenRate * FP_ONE + 0.5f);
        cfg.maxCellTroopsFP = maxTroops < 1.0f ? FP_ONE : static_cast<int32_t>(maxTroops * FP_ONE + 0.5f);
        cfg.organicExpansion = organic;
        rebuildActiveSet();
    }

    float getTeamConfigParam(int teamId, int paramIdx) const {
        if (teamId <= 0 || teamId >= teamConfigs.size()) return 0.0f;
        const TeamConfig& cfg = teamConfigs[teamId];
        switch (paramIdx) {
            case 0: return static_cast<float>(cfg.priorityOrder[0]);
            case 1: return static_cast<float>(cfg.priorityOrder[1]);
            case 2: return static_cast<float>(cfg.priorityOrder[2]);
            case 3: return static_cast<float>(cfg.priorityOrder[3]);
            case 4: return static_cast<float>(cfg.conquestThresholdFP) / static_cast<float>(FP_ONE);
            case 5: return static_cast<float>(cfg.emissionPeriod);
            case 6: return static_cast<float>(cfg.maxAreaLimit);
            case 7: return static_cast<float>(cfg.expansionFrictionFP) / static_cast<float>(FP_ONE);
            case 8: return cfg.multiDirTransit ? 1.0f : 0.0f;
            case 9: return static_cast<float>(cfg.troopRegenFP) / static_cast<float>(FP_ONE);
            case 10: return static_cast<float>(cfg.maxCellTroopsFP) / static_cast<float>(FP_ONE);
            case 11: return cfg.organicExpansion ? 1.0f : 0.0f;
            default: return 0.0f;
        }
    }

    uint32_t getTeamColorRGBA(int teamId) const {
        return (teamId > 0 && teamId < teamConfigs.size()) ? teamConfigs[teamId].colorRGBA : packRGBA(100, 116, 139);
    }

    // Consultas de estado, telemetria y memoria compartida
    int getWidth() const { return width; }
    int getHeight() const { return height; }
    int getTick() const { return currentTick; }
    int getNationCount() const { return totalNations; }
    int getActiveCellCount() const { return activeCells.size(); }
    uint32_t getRenderVersion() const { return renderVersion; }
    int getTeamCount() const { return teamConfigs.size() - 1; }
    bool isTeamActive(int teamId) const { return teamId > 0 && teamId < teamConfigs.size() && teamConfigs[teamId].active; }

    int getTeamArea(int teamId) const { return (teamId > 0 && teamId < teamAreas.size()) ? teamAreas[teamId] : 0; }
    float getTeamTroops(int teamId) const {
        return (teamId > 0 && teamId < teamTroopsFP.size())
            ? static_cast<float>(teamTroopsFP[teamId]) / static_cast<float>(FP_ONE) : 0.0f;
    }
    int getTeamNationCount(int teamId) const {
        return (teamId > 0 && teamId < teamNationCounts.size()) ? teamNationCounts[teamId] : 0;
    }

    uint32_t* getPixelBufferPtr() { return pixelBuffer.rawData(); }
    uint16_t* getMetaBufferPtr() { return metaBuffer.rawData(); }
    float* getGraphNodesPtr() { return graphNodesBuf.rawData(); }
    int getGraphNodeCount() const { return graphNodesBuf.size() / 5; }
    float* getGraphEdgesPtr() { return graphEdgesBuf.rawData(); }
    int getGraphEdgeCount() const { return graphEdgesBuf.size() / 4; }

    int32_t* getHistoryTreePtr() { return historyTree.getExportBufferPtr(); }
    int getHistoryNodeCount() const { return historyTree.getNodeCount(); }

    int getCellNationSize(int x, int y) const {
        if (!inBounds(x, y)) return 0;
        int nId = currentGrid[index(x, y)].nationId;
        return (nId > 0 && nId < nationSizes.size()) ? nationSizes[nId] : 0;
    }
    int getCellNationId(int x, int y) const { return inBounds(x, y) ? currentGrid[index(x, y)].nationId : 0; }
    int getCellTroopsFP(int x, int y) const { return inBounds(x, y) ? currentGrid[index(x, y)].localTroopsFP : 0; }
    int getCellTerrain(int x, int y) const { return inBounds(x, y) ? currentGrid[index(x, y)].terrain : 0; }
    int getCellTeam(int x, int y) const { return inBounds(x, y) ? currentGrid[index(x, y)].teamId : 0; }
    int getCellGrowthFactor(int x, int y) const { return inBounds(x, y) ? currentGrid[index(x, y)].growthFactor : GROWTH_BASE; }
    int getCellFlowDir(int x, int y) const { return inBounds(x, y) ? currentGrid[index(x, y)].flowDir : 0; }
};

} // namespace Engine

#endif // SIMULATION_ENGINE_HPP