#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <stdint.h>  // Include for uintptr_t

// Declare the functions as extern to avoid multiple definitions
extern uintptr_t ptr_demangle(uintptr_t p);
extern uintptr_t ptr_mangle(uintptr_t p);
extern void start_thunk();

// Sample function for returning a string
void *return_string() {
    char *result = malloc(20 * sizeof(char));
    if (result == NULL) {
        perror("Failed to allocate memory");
        pthread_exit(NULL);  // Exit with NULL if allocation fails
    }
    strcpy(result, "Hello, world!");
    return result;
}

// Sample function for returning an integer
void *return_int() {
    int *result = malloc(sizeof(int));
    if (result == NULL) {
        perror("Failed to allocate memory");
        pthread_exit(NULL);  // Exit with NULL if allocation fails
    }
    *result = 42;
    return result;
}

// Define a complex structure for the complex return type test
typedef struct {
    int a;
    double b;
} complex_struct;

// Sample function for returning a complex structure
void *return_complex() {
    complex_struct *result = malloc(sizeof(complex_struct));
    if (result == NULL) {
        perror("Failed to allocate memory");
        pthread_exit(NULL);  // Exit with NULL if allocation fails
    }
    result->a = 5;
    result->b = 10.5;
    return result;
}

int main() {
    pthread_t thread;
    void *value;

    // Test 8: String return value from thread
    if (pthread_create(&thread, NULL, return_string, NULL) != 0) {
        perror("Failed to create thread for return_string");
        exit(EXIT_FAILURE);
    }
    if (pthread_join(thread, &value) != 0) {
        perror("Failed to join thread for return_string");
        exit(EXIT_FAILURE);
    }
    if (value != NULL) {
        printf("String return value: %s\n", (char *)value);
        free(value);  // Free the dynamically allocated memory
    }

    // Test 9: Integer return value from thread
    if (pthread_create(&thread, NULL, return_int, NULL) != 0) {
        perror("Failed to create thread for return_int");
        exit(EXIT_FAILURE);
    }
    if (pthread_join(thread, &value) != 0) {
        perror("Failed to join thread for return_int");
        exit(EXIT_FAILURE);
    }
    if (value != NULL) {
        printf("Integer return value: %d\n", *(int *)value);
        free(value);  // Free the dynamically allocated memory
    }

    // Test 10: Complex structure return value from thread
    if (pthread_create(&thread, NULL, return_complex, NULL) != 0) {
        perror("Failed to create thread for return_complex");
        exit(EXIT_FAILURE);
    }
    if (pthread_join(thread, &value) != 0) {
        perror("Failed to join thread for return_complex");
        exit(EXIT_FAILURE);
    }
    if (value != NULL) {
        complex_struct *complex_result = (complex_struct *)value;
        printf("Complex return value: a = %d, b = %f\n", complex_result->a, complex_result->b);
        free(value);  // Free the dynamically allocated memory
    }

    return 0;
}
