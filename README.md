# README

This guide explains how to compile and execute the helper program along with your solution.

## Steps to Compile and Execute

1. **Compile the Helper Program**
   Use the following command to compile the helper program:
   ```bash
   gcc helper-program.c -lpthread -o helper
   ```

2. **Compile Your Solution**
   Replace `<student-program-here>` with your solution filename (e.g., `solution.c`) and use this command:
   ```bash
   gcc solution.c -lpthread -o solution
   ```

3. **Setup Folder Structure**
   Ensure the following files are in the same folder:
   - The **test case file**
   - The **helper executable** (compiled from `helper-program.c`)
   - The **solution executable** (compiled from `solution.c`)

4. **Run the Helper Program**
   To execute the helper with a specific test case, use this command:
   ```bash
   ./helper <testcase-number-here>
   ```
   Replace `<testcase-number-here>` with the desired test case number.

## Example Usage
Assuming you have `solution.c`, `helper-program.c`, and a test case ready:

1. Compile the helper program:
   ```bash
   gcc helper-program.c -lpthread -o helper
   ```

2. Compile the solution:
   ```bash
   gcc solution.c -lpthread -o solution
   ```

3. Execute the helper program with test case `1`:
   ```bash
   ./helper 1
   ```

This will execute the solution program with the specified test case.
