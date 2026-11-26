/*
 * Archivo: src/core/HistoryTree.hpp
 * Proposito: Implementacion desde cero de un Arbol N-ario de historial (Undo/Redo ramificado)
 *            basado en deltas de celdas y transacciones atomicas.
 * Declaracion de transparencia academica:
 *   Codigo asistido/generado mediante inteligencia artificial: 100% (plantilla base para revision y adaptacion).
 */

#ifndef HISTORY_TREE_HPP
#define HISTORY_TREE_HPP

#include "LinearADTs.hpp"
#include <cstdint>

namespace Core {

template <typename CellT>
struct CellDelta {
    int index;
    CellT before;
    CellT after;

    CellDelta() : index(0), before(), after() {}
    CellDelta(int idx, const CellT& b, const CellT& a)
        : index(idx), before(b), after(a) {}
};

template <typename CellT>
struct HistoryNode {
    int id;
    int parentId;
    int tick;
    int kind; // 0 = Raiz/Inicial, 1 = Simulacion, 2 = Edicion
    int depth;
    int lastChildIdx;
    HistoryNode* parent;
    DynamicArray<HistoryNode*> children;
    DynamicArray<CellDelta<CellT>> deltas;

    HistoryNode(int nodeId, HistoryNode* p, int t, int k)
        : id(nodeId),
          parentId(p ? p->id : -1),
          tick(t),
          kind(k),
          depth(p ? p->depth + 1 : 0),
          lastChildIdx(-1),
          parent(p),
          children(2),
          deltas(4) {}
};

/**
 * TDA Arbol N-ario de Historial.
 * Permite retroceder, rehacer y saltar a cualquier rama temporal mediante busqueda de Ancestro Comun Mas Bajo (LCA).
 */
template <typename CellT>
class HistoryTree {
private:
    DynamicArray<HistoryNode<CellT>*> nodeById;
    HistoryNode<CellT>* currentNode;

    // Estado de transaccion atomica en curso
    bool inTransaction;
    int pendingKind;
    DynamicArray<CellDelta<CellT>> pendingDeltas;
    DynamicArray<int> touchedPos;     // Mapea cellIndex -> indice en pendingDeltas (-1 si no tocada)
    DynamicArray<int> touchedIndices; // Lista de celdas tocadas para limpieza en O(k)

    // Bufer plano para visualizacion del arbol en JavaScript: [id, parentId, tick, kind, isCurrent]
    DynamicArray<int32_t> flatExportBuf;

    // Complejidad temporal: O(N), donde N es el numero total de nodos del arbol
    void freeAllNodes() {
        for (int i = 0; i < nodeById.size(); ++i) {
            delete nodeById[i];
        }
        nodeById.clear();
        currentNode = nullptr;
    }

    // Complejidad temporal: O(k), donde k es el numero de celdas modificadas en la transaccion
    void clearTouched() {
        for (int i = 0; i < touchedIndices.size(); ++i) {
            touchedPos[touchedIndices[i]] = -1;
        }
        touchedIndices.clear();
        pendingDeltas.clear();
    }

    // Complejidad temporal: O(N)
    void rebuildExportBuffer() {
        flatExportBuf.clear();
        int curId = currentNode ? currentNode->id : -1;
        for (int i = 0; i < nodeById.size(); ++i) {
            HistoryNode<CellT>* n = nodeById[i];
            flatExportBuf.pushBack(n->id);
            flatExportBuf.pushBack(n->parentId);
            flatExportBuf.pushBack(n->tick);
            flatExportBuf.pushBack(n->kind);
            flatExportBuf.pushBack(n->id == curId ? 1 : 0);
        }
    }

public:
    HistoryTree()
        : nodeById(32), currentNode(nullptr), inTransaction(false),
          pendingKind(0), pendingDeltas(16), touchedPos(16),
          touchedIndices(16), flatExportBuf(64) {}

    HistoryTree(const HistoryTree&) = delete;
    HistoryTree& operator=(const HistoryTree&) = delete;

    // Complejidad temporal: O(N)
    ~HistoryTree() {
        freeAllNodes();
    }

    // Reinicia el arbol con una nueva raiz para un tablero de tamano gridSize. Complejidad: O(N + V).
    void reset(int gridSize, int initialTick = 0) {
        freeAllNodes();
        inTransaction = false;
        pendingDeltas.clear();
        touchedIndices.clear();
        touchedPos.resize(gridSize > 0 ? gridSize : 1, -1);
        touchedPos.fill(-1);

        HistoryNode<CellT>* root = new HistoryNode<CellT>(0, nullptr, initialTick, 0);
        nodeById.pushBack(root);
        currentNode = root;
        rebuildExportBuffer();
    }

    // Inicia una transaccion atomica. Complejidad: O(1).
    void beginTransaction(int kind) {
        if (inTransaction) {
            clearTouched();
        }
        inTransaction = true;
        pendingKind = kind;
    }

    // Registra la modificacion de una celda preservando su estado inicial original. Complejidad: O(1) amortizado.
    void recordChange(int cellIndex, const CellT& before, const CellT& after) {
        if (!inTransaction || cellIndex < 0 || cellIndex >= touchedPos.size()) return;
        int pos = touchedPos[cellIndex];
        if (pos == -1) {
            touchedPos[cellIndex] = pendingDeltas.size();
            touchedIndices.pushBack(cellIndex);
            pendingDeltas.pushBack(CellDelta<CellT>(cellIndex, before, after));
        } else {
            pendingDeltas[pos].after = after;
        }
    }

    // Consolida la transaccion en un nuevo nodo hijo sin eliminar ramas hermanas. Complejidad: O(k + N).
    bool commitTransaction(int newTick) {
        if (!inTransaction || !currentNode) return false;
        inTransaction = false;

        DynamicArray<CellDelta<CellT>> compact(pendingDeltas.size() > 0 ? pendingDeltas.size() : 1);
        for (int i = 0; i < pendingDeltas.size(); ++i) {
            if (!(pendingDeltas[i].before == pendingDeltas[i].after)) {
                compact.pushBack(pendingDeltas[i]);
            }
        }
        clearTouched();

        if (compact.isEmpty() && newTick == currentNode->tick) {
            return false;
        }

        int newId = nodeById.size();
        HistoryNode<CellT>* child = new HistoryNode<CellT>(newId, currentNode, newTick, pendingKind);
        child->deltas = static_cast<DynamicArray<CellDelta<CellT>>&&>(compact);

        currentNode->children.pushBack(child);
        currentNode->lastChildIdx = currentNode->children.size() - 1;
        nodeById.pushBack(child);
        currentNode = child;
        rebuildExportBuffer();
        return true;
    }

    // Deshacer (subir al padre en el arbol). Complejidad: O(k_nodo + N).
    bool undo(DynamicArray<CellT>& grid, int& outTick) {
        if (!currentNode || !currentNode->parent) return false;

        for (int i = currentNode->deltas.size() - 1; i >= 0; --i) {
            const CellDelta<CellT>& d = currentNode->deltas[i];
            grid[d.index] = d.before;
        }

        HistoryNode<CellT>* p = currentNode->parent;
        for (int c = 0; c < p->children.size(); ++c) {
            if (p->children[c] == currentNode) {
                p->lastChildIdx = c;
                break;
            }
        }
        currentNode = p;
        outTick = currentNode->tick;
        rebuildExportBuffer();
        return true;
    }

    // Rehacer (descender por el ultimo hijo activo). Complejidad: O(k_nodo + N).
    bool redo(DynamicArray<CellT>& grid, int& outTick) {
        if (!currentNode || currentNode->children.isEmpty()) return false;

        int cIdx = currentNode->lastChildIdx;
        if (cIdx < 0 || cIdx >= currentNode->children.size()) {
            cIdx = currentNode->children.size() - 1;
        }
        HistoryNode<CellT>* target = currentNode->children[cIdx];

        for (int i = 0; i < target->deltas.size(); ++i) {
            const CellDelta<CellT>& d = target->deltas[i];
            grid[d.index] = d.after;
        }

        currentNode = target;
        outTick = currentNode->tick;
        rebuildExportBuffer();
        return true;
    }

    // Salto directo a cualquier nodo del arbol usando el Ancestro Comun Mas Bajo (LCA).
    // Complejidad temporal: O(dist(u, v) * k_promedio + N).
    bool jumpToNode(int targetId, DynamicArray<CellT>& grid, int& outTick) {
        if (!currentNode || targetId < 0 || targetId >= nodeById.size()) return false;
        HistoryNode<CellT>* target = nodeById[targetId];
        if (!target || target == currentNode) return false;

        DynamicArray<HistoryNode<CellT>*> upPath(16);
        DynamicArray<HistoryNode<CellT>*> downPath(16);

        HistoryNode<CellT>* a = currentNode;
        HistoryNode<CellT>* b = target;

        while (a->depth > b->depth) {
            upPath.pushBack(a);
            a = a->parent;
        }
        while (b->depth > a->depth) {
            downPath.pushBack(b);
            b = b->parent;
        }
        while (a != b) {
            upPath.pushBack(a);
            a = a->parent;
            downPath.pushBack(b);
            b = b->parent;
        }

        // Revertir desde el nodo actual hasta el LCA
        for (int i = 0; i < upPath.size(); ++i) {
            HistoryNode<CellT>* n = upPath[i];
            for (int j = n->deltas.size() - 1; j >= 0; --j) {
                grid[n->deltas[j].index] = n->deltas[j].before;
            }
        }

        // Avanzar desde el LCA hasta el nodo destino
        for (int i = downPath.size() - 1; i >= 0; --i) {
            HistoryNode<CellT>* n = downPath[i];
            for (int j = 0; j < n->deltas.size(); ++j) {
                grid[n->deltas[j].index] = n->deltas[j].after;
            }
            if (n->parent) {
                for (int c = 0; c < n->parent->children.size(); ++c) {
                    if (n->parent->children[c] == n) {
                        n->parent->lastChildIdx = c;
                        break;
                    }
                }
            }
        }

        currentNode = target;
        outTick = currentNode->tick;
        rebuildExportBuffer();
        return true;
    }

    bool isInTransaction() const { return inTransaction; }
    int getNodeCount() const { return nodeById.size(); }
    int getCurrentNodeId() const { return currentNode ? currentNode->id : -1; }
    int32_t* getExportBufferPtr() { return flatExportBuf.rawData(); }
};

} // namespace Core

#endif // HISTORY_TREE_HPP