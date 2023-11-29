
#include "interface.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#define HASH_SIZE 1229
#define MAX_SIZE 1000

//////////////////// STRUCTS AND DEFINITIONS ////////////////////
typedef enum { UNVISITED, VISITING} cell_state;
typedef enum { NUMBER, TEXT, FORMULA, ERROR} cell_type;
typedef struct cell cell;

///// CELL STRUCTURE
struct cell {
    // Position of cell
    ROW row;
    COL col;

    // Cell contains number or string
    union {
        double number_value;
        char *text_value;
    } content;

    // Computed value if cell contains formula
    double computed_value;

    // Formula string and define cell type
    char *formula;
    cell_type type;

    // The original input of the cell
    char *original_input;

    // Array of "dependant" cells (cells that depend on other cells i.e for their formula)
    cell **dependents;
    int dependents_count;
    int dependents_capacity;

    // The state of the cell
    cell_state state;
};

///// NODE STRUCTURE FOR HASH
typedef struct node {
    // Hash key of node
    char key[50];

    // Value of cell
    cell value;
    struct node *next;

} node;

node *spreadsheet[MAX_SIZE];



//////////////////// HELPER FUNCTIONS ////////////////////

//// HASH MAP FUNCTION
unsigned long hash(char *str) {
    // Set hash value
    unsigned long hash = 5381;
    int c;

    // Loop over each character, compute hash value
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;

    // Return modulus to fit in the table
    return hash % MAX_SIZE;
}

//// ERROR SET FUNCTION
void set_error_and_update(cell *current, char *error_message) {
    // Free the pre-existed memory
    if (current->content.text_value != NULL) {
        free(current->content.text_value);
    }

    // Set cell type to ERROR
    current->type = ERROR;

    // Replace the cell with the error message, update display
    current->content.text_value = strdup(error_message);
    update_cell_display(current->row, current->col, current->content.text_value);
}


//////////////////// CELL FUNCTIONS ////////////////////

//// CREATE NEW CELL FUNCTION
cell *create_cell(ROW row, COL col, char *text) {
    // Buffer to store key, format key
    char key[50];
    snprintf(key, sizeof(key), "%d,%d", row, col);

    // Hash key and put into index
    unsigned int index = hash(key);

    // Allocate memory for a new node
    node *new_node = malloc(sizeof(node));

    // Copy the key to the new node, insert at beginning of list
    strcpy(new_node->key, key);
    new_node->next = spreadsheet[index];
    spreadsheet[index] = new_node;

    // Get a pointer to the cell in the new node
    cell *current = &new_node->value;

    // Position of cell
    current->row = row;
    current->col = col;

    // Initialize empty dependant array
    current->dependents = NULL;
    current->dependents_count = 0;
    current->dependents_capacity = 0;

    // Ground state, set original input to text
    current->state = UNVISITED;
    current->original_input = strdup(text);

    return current;
}

//// DEPENDANT ARRAY FUNCTION
void add_dependent(cell *current, cell *dependent) {
    // Double capacity if array is full, reallocate
    if (current->dependents_count == current->dependents_capacity) {
        current->dependents_capacity *= 2;
        current->dependents = realloc(current->dependents, current->dependents_capacity * sizeof(cell *));
    }

    // Add dependent cell
    current->dependents[current->dependents_count++] = dependent;
}

//// FREE CELL FUNCTION
void free_cell(cell *current) {
    //Check for types, free the appropriate values
    if (current->type == FORMULA) {
        free(current->formula);
    }

    else if (current->type == TEXT || current->type == ERROR) {
        free(current->content.text_value);
    }

    // Free dependant array
    free(current->dependents);

    // Free original input if valid
    if (current->original_input != NULL) {
        free(current->original_input);
    }

}

//// FIND A CELL FUNCTION
cell *find_cell(ROW row, COL col) {
    // Store key, format key, compute hash
    char key[50];
    sprintf(key, "%d,%d", row, col);
    unsigned int index = hash(key);

    // Get first node in linked list
    node *current = spreadsheet[index];

    // Loop over the linked list until cell is found
    while (current != NULL) {
        if (strcmp(current->key, key) == 0) {
            return &current->value;
        }
        current = current->next;
    }

    // Cell not found, return NULL
    return NULL;
}

//// CLEARING A CELL FUNCTION
void clear_cell(ROW row, COL col) {
    // Create key
    char key[50];
    sprintf(key, "%d,%d", row, col);

    // Compute hash of key, get first node
    unsigned int index = hash(key);
    node *current = spreadsheet[index];

    // Set prev node to NULL
    node *prev = NULL;

    // Loop over the nodes in the linked list
    while (current != NULL) {
        // If the key of the current node matches the key of the cell
        if (strcmp(current->key, key) == 0) {
            // If the current node is the first node remove it
            if (prev == NULL) {
                spreadsheet[index] = current->next;
            }
                // Else remove the current node
            else {
                prev->next = current->next;
            }

            // If cell is a FORMULA, free memory
            if (current->value.type == FORMULA) {
                free(current->value.formula);
            }
                // If the cell is TEXT or ERROR, free memory
            else if (current->value.type == TEXT || current->value.type == ERROR) {
                free(current->value.content.text_value);
            }

            // Free node memory, update cell display
            free(current);
            update_cell_display(row, col, "");
            return;
        }

        //Goto next node
        prev = current;
        current = current->next;
    }
}

//// EVALUATE A FORMULA IN A CELL FUNCTION
double evaluate_formula(cell *current, char *formula) {
    // Set the state of the cell to VISITING to detect circular dependencies
    current->state = VISITING;

    // Initialize the result of the formula to 0
    double result = 0;
    char *result_str = NULL;

    // Tokenize the formula by the '+' operator
    char *token = strtok(formula, "+");

    // Loop over the tokens in the formula
    while (token != NULL) {
        // If the token is a cell reference
        if (isalpha(token[0])) {

            // Compute cell position and find
            COL col = token[0] - 'A';
            ROW row = atoi(token + 1) - 1;
            cell *cell = find_cell(row, col);

            // If the cell does not exist, set an error and return NaN
            if (cell == NULL) {
                set_error_and_update(current, "ERROR: invalid cell reference");
                current->state = UNVISITED;
                return NAN;
            }

            // If the cell is currently being visited, set an error for circular dependency and return NaN
            if (cell->state == VISITING) {
                set_error_and_update(current, "ERROR: Circular Dependency");
                current->state = UNVISITED;
                return NAN;
            }

            // If the cell contains a number, add it to the result
            if (cell->type == NUMBER) {
                result += cell->content.number_value;
            }

            // If the cell contains a formula, evaluate it and add the result to the result
            else if (cell->type == FORMULA) {
                //Accounting for formula inside a formula
                double subformula_result = evaluate_formula(cell, cell->formula);

                //If no subformula, return
                if (isnan(subformula_result)) {
                    current->state = UNVISITED;
                    return NAN;
                }
                result += subformula_result;
            }

            // Else, cell type is a string
            else if (cell->type == TEXT) {
                // If result_string is null, set result_string to first string
                if (result_str == NULL) {
                    result_str = strdup(cell->content.text_value);
                }

                //Else, make a new combined string and copy both strings
                else {
                    char *new_result_str = malloc(strlen(result_str) + strlen(cell->content.text_value) + 1);
                    strcpy(new_result_str, result_str);
                    strcat(new_result_str, cell->content.text_value);
                    free(result_str);
                    result_str = new_result_str;
                }
            }

            // Add the current cell as a dependent of the cell
            add_dependent(cell, current);
        }

        // Else token is a number, add to result
        else {
            result += atof(token);
        }

        // Get the next token in the formula
        token = strtok(NULL, "+");

    }

    // Set the state of the cell to UNVISITED after the evaluation, return result
    current->state = UNVISITED;

    // Update cell if string was added
    if (result_str != NULL) {
        current->type = TEXT;
        current->content.text_value = result_str;
        return NAN;
    }

    return result;
}

//// SETTING CELL VALUE FUNCTION
void set_cell_value(ROW row, COL col, char *text) {
    // Find the cell at the given row and column
    cell *current = find_cell(row, col);

    // If the cell does not exist, create new cell
    if (current == NULL) {
        current = create_cell(row, col, text);
    }

    // Else cell exists
    else {
        // If the cell already has an original input, free it before setting a new one
        if (current->original_input != NULL) {
            free(current->original_input);
        }

        // Set the original input to the given text
        current->original_input = strdup(text);
    }

    // If the cells type is FORMULA, free the formula before setting a new one
    if (current->type == FORMULA) {
        free(current->formula);
    }

    // Else cells type is text or error
    else if (current->type == TEXT || current->type == ERROR) {
        // Free value before setting new one
        free(current->content.text_value);
    }

    // If first character of input text is '=', evaluate as formula
    if (text[0] == '=') {
        // Set the cell's type to FORMULA and skip '='
        current->type = FORMULA;
        current->formula = strdup(text + 1);


        // Evaluate formula
        double formula_result = evaluate_formula(current, current->formula);

        // If formula result is not number, it returns nan
        if (isnan(formula_result)) {
            // If the cell's type is FORMULA, update the cell's display with the error message
            if (current->type == FORMULA) {
                current->type = ERROR;
                current->content.text_value = strdup(current->original_input);
                update_cell_display(row, col, current->content.text_value);
            }
            return;
        }

        // Else formula result is number
        else {
            // Set cell type and value
            current->computed_value = formula_result;
            current->type = NUMBER;
            current->content.number_value = current->computed_value;

            // Convert value to string and update display
            char computed_value[50];
            snprintf(computed_value, sizeof(computed_value), "%.1f", current->computed_value);
            update_cell_display(row, col, computed_value);
        }

        // Loop over all dependents of the current cell
        for (int i = 0; i < current->dependents_count; i++) {
            //Get dependant cell
            cell *dependent = current->dependents[i];

            // If the dependent cell's dependancy is the same as the current cell, set error for self reference (circular error)
            if (dependent->dependents[i] == current) {
                set_error_and_update(current, "ERROR: Self Reference");
                continue;
            }


            //Reevaluate formula at dependant cell
            dependent->content.number_value = evaluate_formula(dependent, dependent->formula);

            // If result is NAN it's text or error
            if (isnan(dependent->content.number_value)) {
                // Update display of dependant cell
                update_cell_display(dependent->row, dependent->col, dependent->content.text_value);
            }

            // Else result is number
            else {
                // Set type and value
                dependent->type = NUMBER;
                dependent->content.number_value = dependent->content.number_value;

                // Convert the number value to a string
                char dependent_value[50];
                snprintf(dependent_value, sizeof(dependent_value), "%.1f", dependent->content.number_value);

                // Update the display
                update_cell_display(dependent->row, dependent->col, dependent_value);
            }
        }
    }

    // Else text is not formula
    else {
        // Try to convert the text to a number
        char *end;
        double number_value = strtod(text, &end);

        // If the entire text is a valid number
        if (*end == '\0') {
            // Set the type of the cell to NUMBER and set its number value to the converted number
            current->type = NUMBER;
            current->content.number_value = number_value;

        }
        // Entire text is not valid number
        else {
            // Set cell type and text_value
            current->type = TEXT;
            current->content.text_value = strdup(text);
        }

        // Update the display
        update_cell_display(row, col, text);
    }
}

//// RETURN ORIGINAL STRING FUNCTION
char *get_textual_value(ROW row, COL col) {
    // Find cell
    cell *current = find_cell(row, col);

    // If cell exists
    if (current != NULL) {
/*        // If cell type is NUMBER, return the computed value as a string
        if (current->type == NUMBER) {
            char *value = malloc(50 * sizeof(char));
            snprintf(value, 50, "%.1f", current->content.number_value);
            return value;
        }*/

        // If cell type is TEXT, return the text value
        if (current->type == TEXT) {
            return strdup(current->content.text_value);
        }

        // Else return the original input string
        else {
            return strdup(current->original_input);
        }
    }

    // Else cell does not exist
    else {
        printf("Cell (%d, %d) does not exist\n", row, col);
        return NULL;
    }

}

//////////////////// MODEL FUNCTIONS ////////////////////

//// SPREADSHEET INITIALIZATION FUNCTION
void model_init() {
    for (int i = 0; i < MAX_SIZE; i++) {
        spreadsheet[i] = NULL;
    }
}

//// SPREADSHEET FREEING FUNCTION
void model_destroy() {
    for (int i = 0; i < HASH_SIZE; i++) {
        for (node *current = spreadsheet[i]; current != NULL; ) {
            node *next = current->next;
            free_cell(&current->value);
            free(current);
            current = next;
        }
    }
}




