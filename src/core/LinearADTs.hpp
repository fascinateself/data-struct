/*
 * Archivo: src/core/LinearADTs.hpp
 * Proposito: Implementacion desde cero de Tipos de Dato Abstractos (TDA) lineales.
 * Estructuras: DynamicArray<T>, CircularQueue<T>, PriorityQueue<T>, Stack<T>.
 * Declaracion de transparencia academica:
 *   Codigo asistido/generado mediante inteligencia artificial: 100% (plantilla base para revision y adaptacion).
 */

#ifndef LINEAR_ADTS_HPP
#define LINEAR_ADTS_HPP

#include <cstdint>

namespace Core {

template <typename T>
void swapValues(T& a, T& b) {
    T temp = static_cast<T&&>(a);
    a = static_cast<T&&>(b);
    b = static_cast<T&&>(temp);
}

/**
 * TDA Arreglo Dinamico con crecimiento geometrico.
 * Invariante: 0 <= count <= cap; data apunta a un bloque contiguo de cap elementos.
 */
template <typename T>
class DynamicArray {
private:
    T* data;
    int count;
    int cap;

    // Complejidad temporal: O(n), Espacial: O(n)
    void reallocate(int newCap) {
        T* newData = new T[newCap];
        for (int i = 0; i < count; ++i) {
            newData[i] = static_cast<T&&>(data[i]);
        }
        delete[] data;
        data = newData;
        cap = newCap;
    }

public:
    // Complejidad temporal: O(1)
    explicit DynamicArray(int initialCapacity = 8)
        : count(0), cap(initialCapacity > 0 ? initialCapacity : 8) {
        data = new T[cap];
    }

    // Complejidad temporal: O(n)
    DynamicArray(const DynamicArray& other)
        : count(other.count), cap(other.cap) {
        data = new T[cap];
        for (int i = 0; i < count; ++i) {
            data[i] = other.data[i];
        }
    }

    // Complejidad temporal: O(1)
    DynamicArray(DynamicArray&& other) noexcept
        : data(other.data), count(other.count), cap(other.cap) {
        other.data = nullptr;
        other.count = 0;
        other.cap = 0;
    }

    // Complejidad temporal: O(n)
    DynamicArray& operator=(const DynamicArray& other) {
        if (this != &other) {
            delete[] data;
            count = other.count;
            cap = other.cap;
            data = new T[cap];
            for (int i = 0; i < count; ++i) {
                data[i] = other.data[i];
            }
        }
        return *this;
    }

    // Complejidad temporal: O(1)
    DynamicArray& operator=(DynamicArray&& other) noexcept {
        if (this != &other) {
            delete[] data;
            data = other.data;
            count = other.count;
            cap = other.cap;
            other.data = nullptr;
            other.count = 0;
            other.cap = 0;
        }
        return *this;
    }

    // Complejidad temporal: O(1)
    ~DynamicArray() {
        delete[] data;
    }

    // Complejidad temporal: O(1) amortizado, O(n) en peor caso por redimensionamiento
    void pushBack(const T& value) {
        if (count == cap) {
            reallocate(cap == 0 ? 8 : cap * 2);
        }
        data[count++] = value;
    }

    // Complejidad temporal: O(1)
    void popBack() {
        if (count > 0) {
            --count;
        }
    }

    // Complejidad temporal: O(n)
    void resize(int newSize, const T& defaultValue = T()) {
        if (newSize > cap) {
            reallocate(newSize);
        }
        for (int i = count; i < newSize; ++i) {
            data[i] = defaultValue;
        }
        count = newSize;
    }

    // Complejidad temporal: O(n)
    void fill(const T& value) {
        for (int i = 0; i < count; ++i) {
            data[i] = value;
        }
    }

    // Complejidad temporal: O(1)
    void clear() { count = 0; }

    // Complejidad temporal: O(1)
    T& operator[](int index) { return data[index]; }

    // Complejidad temporal: O(1)
    const T& operator[](int index) const { return data[index]; }

    // Complejidad temporal: O(1)
    T* rawData() { return data; }

    // Complejidad temporal: O(1)
    int size() const { return count; }

    // Complejidad temporal: O(1)
    bool isEmpty() const { return count == 0; }
};

/**
 * TDA Cola Circular dinamica.
 * Aplicacion: Recorrido BFS de componentes conexas y cola de ordenes del jugador.
 */
template <typename T>
class CircularQueue {
private:
    T* buffer;
    int head;
    int tail;
    int count;
    int cap;

    // Complejidad temporal: O(n), Espacial: O(n)
    void grow() {
        int newCap = cap * 2;
        T* newBuffer = new T[newCap];
        for (int i = 0; i < count; ++i) {
            newBuffer[i] = static_cast<T&&>(buffer[(head + i) % cap]);
        }
        delete[] buffer;
        buffer = newBuffer;
        head = 0;
        tail = count;
        cap = newCap;
    }

public:
    // Complejidad temporal: O(1)
    explicit CircularQueue(int initialCapacity = 64)
        : head(0), tail(0), count(0), cap(initialCapacity > 0 ? initialCapacity : 64) {
        buffer = new T[cap];
    }

    CircularQueue(const CircularQueue&) = delete;
    CircularQueue& operator=(const CircularQueue&) = delete;

    // Complejidad temporal: O(1)
    ~CircularQueue() {
        delete[] buffer;
    }

    // Complejidad temporal: O(1) amortizado, O(n) al redimensionar
    void enqueue(const T& value) {
        if (count == cap) {
            grow();
        }
        buffer[tail] = value;
        tail = (tail + 1) % cap;
        ++count;
    }

    // Complejidad temporal: O(1)
    T dequeue() {
        T value = buffer[head];
        head = (head + 1) % cap;
        --count;
        return value;
    }

    // Complejidad temporal: O(1)
    void clear() {
        head = 0;
        tail = 0;
        count = 0;
    }

    // Complejidad temporal: O(1)
    bool isEmpty() const { return count == 0; }

    // Complejidad temporal: O(1)
    int size() const { return count; }
};

/**
 * TDA Cola con Prioridad implementada mediante Monticulo Binario Maximo (Max-Heap).
 * Aplicacion: Resolucion determinista de conflictos por fuerza en celdas destino.
 */
template <typename T>
class PriorityQueue {
private:
    DynamicArray<T> heap;

    // Complejidad temporal: O(log n)
    void siftUp(int idx) {
        while (idx > 0) {
            int parent = (idx - 1) / 2;
            if (heap[parent] < heap[idx]) {
                swapValues(heap[parent], heap[idx]);
                idx = parent;
            } else {
                break;
            }
        }
    }

    // Complejidad temporal: O(log n)
    void siftDown(int idx) {
        int n = heap.size();
        while (true) {
            int largest = idx;
            int left = 2 * idx + 1;
            int right = 2 * idx + 2;

            if (left < n && heap[largest] < heap[left]) {
                largest = left;
            }
            if (right < n && heap[largest] < heap[right]) {
                largest = right;
            }
            if (largest != idx) {
                swapValues(heap[idx], heap[largest]);
                idx = largest;
            } else {
                break;
            }
        }
    }

public:
    PriorityQueue() : heap(8) {}

    // Complejidad temporal: O(log n)
    void push(const T& value) {
        heap.pushBack(value);
        siftUp(heap.size() - 1);
    }

    // Complejidad temporal: O(log n)
    T pop() {
        T topVal = heap[0];
        heap[0] = heap[heap.size() - 1];
        heap.popBack();
        if (!heap.isEmpty()) {
            siftDown(0);
        }
        return topVal;
    }

    // Complejidad temporal: O(1)
    void clear() { heap.clear(); }

    // Complejidad temporal: O(1)
    bool isEmpty() const { return heap.isEmpty(); }

    // Complejidad temporal: O(1)
    int size() const { return heap.size(); }
};

/**
 * TDA Pila (LIFO) con cota superior opcional para control de memoria en historial.
 * Aplicacion: Sistema de retroceso (Undo / Rewind) de estados del tablero.
 */
template <typename T>
class Stack {
private:
    DynamicArray<T> items;
    int maxDepth;

public:
    explicit Stack(int limit = 100) : items(16), maxDepth(limit) {}

    // Complejidad temporal: O(1) amortizado; O(maxDepth) si se descarta el elemento mas antiguo
    void push(const T& value) {
        if (items.size() >= maxDepth) {
            for (int i = 1; i < items.size(); ++i) {
                items[i - 1] = static_cast<T&&>(items[i]);
            }
            items.popBack();
        }
        items.pushBack(value);
    }

    // Complejidad temporal: O(1)
    T pop() {
        T topVal = static_cast<T&&>(items[items.size() - 1]);
        items.popBack();
        return topVal;
    }

    // Complejidad temporal: O(1)
    void clear() { items.clear(); }

    // Complejidad temporal: O(1)
    bool isEmpty() const { return items.isEmpty(); }

    // Complejidad temporal: O(1)
    int size() const { return items.size(); }
};

} // namespace Core

#endif // LINEAR_ADTS_HPP