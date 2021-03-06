/*
 * @BEGIN LICENSE
 *
 * Forte: an open-source plugin to Psi4 (https://github.com/psi4/psi4)
 * that implements a variety of quantum chemistry methods for strongly
 * correlated electrons.
 *
 * Copyright (c) 2012-2017 by its authors (see LICENSE, AUTHORS).
 *
 * The copyrights for code used from other parties are included in
 * the corresponding files.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/.
 *
 * @END LICENSE
 */

#ifndef MULTIDIMENSIONAL_ARRAYS_H
#define MULTIDIMENSIONAL_ARRAYS_H

#include <cstdlib>

template <typename T>
size_t init_matrix(T**& matrix, size_t size1, size_t size2) {
    size_t size = size1 * size2;
    if (size <= 0) {
        matrix = NULL;
    } else {
        matrix = new T*[size1];
        T* vector = new T[size];
        for (size_t i = 0; i < size; ++i)
            vector[i] = static_cast<T>(0); // Zero all the elements
        for (size_t i = 0; i < size1; ++i)
            matrix[i] = &(vector[i * size2]); // Assign the rows pointers
    }
    return (size * sizeof(T));
}

template <typename T>
size_t free_matrix(T**& matrix, size_t size1, size_t size2) {
    size_t size = size1 * size2;
    if (matrix == NULL)
        return (0);
    delete[] matrix[0];
    delete[] matrix;
    matrix = NULL;
    return (size * sizeof(T));
}

template <typename T>
size_t init_matrix(T****& matrix, size_t size1, size_t size2, size_t size3,
                   size_t size4) {
    size_t size = size1 * size2 * size3 * size4;
    if (size <= 0) {
        matrix = NULL;
    } else {
        matrix = new T***[size1];
        for (size_t i = 0; i < size1; i++)
            matrix[i] = new T**[size2];
        for (size_t i = 0; i < size1; i++)
            for (size_t j = 0; j < size2; j++)
                matrix[i][j] = new T*[size3];
        T* vector = new T[size];
        for (size_t i = 0; i < size; i++)
            vector[i] = static_cast<T>(0); // Zero all the elements
        for (size_t i = 0; i < size1; i++)
            for (size_t j = 0; j < size2; j++)
                for (size_t k = 0; k < size3; k++)
                    matrix[i][j][k] =
                        &(vector[i * size2 * size3 * size4 + j * size3 * size4 +
                                 k * size4]); // Assign the rows pointers
    }
    return (size * sizeof(T));
}

template <typename T>
size_t free_matrix(T****& matrix, size_t size1, size_t size2, size_t size3,
                   size_t size4) {
    size_t size = size1 * size2 * size3 * size4;
    if (matrix == NULL)
        return (0);
    delete[] matrix[0][0][0];
    for (size_t i = 0; i < size1; i++)
        for (size_t j = 0; j < size2; j++)
            delete[] matrix[i][j];
    for (size_t i = 0; i < size1; i++)
        delete[] matrix[i];
    delete[] matrix;
    matrix = NULL;
    return (size * sizeof(T));
}

template <typename T>
size_t init_matrix(T******& matrix, size_t size1, size_t size2, size_t size3,
                   size_t size4, size_t size5, size_t size6) {
    size_t size = size1 * size2 * size3 * size4 * size5 * size6;
    if (size <= 0) {
        matrix = NULL;
    } else {
        matrix = new T*****[size1];
        for (size_t i = 0; i < size1; i++)
            matrix[i] = new T****[size2];
        for (size_t i = 0; i < size1; i++)
            for (size_t j = 0; j < size2; j++)
                matrix[i][j] = new T***[size3];
        for (size_t i = 0; i < size1; i++)
            for (size_t j = 0; j < size2; j++)
                for (size_t k = 0; k < size3; k++)
                    matrix[i][j][k] = new T**[size4];
        for (size_t i = 0; i < size1; i++)
            for (size_t j = 0; j < size2; j++)
                for (size_t k = 0; k < size3; k++)
                    for (size_t l = 0; l < size4; l++)
                        matrix[i][j][k][l] = new T*[size5];

        T* vector = new T[size];
        for (size_t i = 0; i < size; i++)
            vector[i] = static_cast<T>(0); // Zero all the elements
        for (size_t i = 0; i < size1; i++)
            for (size_t j = 0; j < size2; j++)
                for (size_t k = 0; k < size3; k++)
                    for (size_t l = 0; l < size4; l++)
                        for (size_t m = 0; m < size5; m++)
                            matrix[i][j][k][l][m] = &(
                                vector[i * size2 * size3 * size4 * size5 *
                                           size6 +
                                       j * size3 * size4 * size5 * size6 +
                                       k * size4 * size5 * size6 +
                                       l * size5 * size6 +
                                       m * size6]); // Assign the rows pointers
    }
    return (size * sizeof(T));
}

template <typename T>
size_t free_matrix(T******& matrix, size_t size1, size_t size2, size_t size3,
                   size_t size4, size_t size5, size_t size6) {
    size_t size = size1 * size2 * size3 * size4 * size5 * size6;
    if (matrix == NULL)
        return (0);
    delete[] matrix[0][0][0][0][0];
    for (size_t i = 0; i < size1; i++)
        for (size_t j = 0; j < size2; j++)
            for (size_t k = 0; k < size3; k++)
                for (size_t l = 0; l < size4; l++)
                    delete[] matrix[i][j][k][l];
    for (size_t i = 0; i < size1; i++)
        for (size_t j = 0; j < size2; j++)
            for (size_t k = 0; k < size3; k++)
                delete[] matrix[i][j][k];
    for (size_t i = 0; i < size1; i++)
        for (size_t j = 0; j < size2; j++)
            delete[] matrix[i][j];
    for (size_t i = 0; i < size1; i++)
        delete[] matrix[i];
    delete[] matrix;
    matrix = NULL;
    return (size * sizeof(T));
}

#endif // MULTIDIMENSIONAL_ARRAYS_H
