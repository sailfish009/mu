//: A simple memory allocator to create space for new variables at runtime.

:(scenarios run)
:(scenario new)
# call new two times with identical arguments; you should get back different results
recipe main [
  1:address:integer/raw <- new integer:type
  2:address:integer/raw <- new integer:type
  3:boolean/raw <- equal 1:address:integer/raw, 2:address:integer/raw
]
+mem: storing 0 in location 3

:(before "End Globals")
size_t Memory_allocated_until = 1000;
size_t Initial_memory_per_routine = 100000;
:(before "End Setup")
Memory_allocated_until = 1000;
Initial_memory_per_routine = 100000;
:(before "End routine Fields")
size_t alloc;
:(replace{} "routine::routine(recipe_number r)")
routine::routine(recipe_number r) :alloc(Memory_allocated_until) {
  alloc = Memory_allocated_until;
  Memory_allocated_until += Initial_memory_per_routine;
  calls.push(call(r));
}

//:: First handle 'type' operands.

:(before "End Mu Types Initialization")
Type_number["type"] = 0;
:(after "Per-recipe Transforms")
// replace type names with type_numbers
if (inst.operation == Recipe_number["new"]) {
  // first arg must be of type 'type'
  assert(inst.ingredients.size() >= 1);
//?   cout << inst.ingredients[0].to_string() << '\n'; //? 1
  assert(isa_literal(inst.ingredients[0]));
  if (inst.ingredients[0].properties[0].second[0] == "type") {
    inst.ingredients[0].set_value(Type_number[inst.ingredients[0].name]);
  }
  trace("new") << inst.ingredients[0].name << " -> " << inst.ingredients[0].value;
}

//:: Now implement the primitive recipe.

:(before "End Primitive Recipe Declarations")
NEW,
:(before "End Primitive Recipe Numbers")
Recipe_number["new"] = NEW;
:(before "End Primitive Recipe Implementations")
case NEW: {
  vector<int> result;
  trace("mem") << "new alloc: " << Current_routine->alloc;
  result.push_back(Current_routine->alloc);
  write_memory(current_instruction().products[0], result);
  vector<int> types;
  types.push_back(current_instruction().ingredients[0].value);
  if (current_instruction().ingredients.size() > 1) {
    // array
    vector<int> capacity = read_memory(current_instruction().ingredients[1]);
    trace("mem") << "array size is " << capacity[0];
    Memory[Current_routine->alloc] = capacity[0];
    Current_routine->alloc += capacity[0]*size_of(types);
  }
  else {
    // scalar
    Current_routine->alloc += size_of(types);
  }
  break;
}

:(scenario new_array)
recipe main [
  1:address:array:integer/raw <- new integer:type, 5:literal
  2:address:integer/raw <- new integer:type
  3:integer/raw <- subtract 2:address:integer/raw, 1:address:array:integer/raw
]
+run: instruction main/0
+mem: array size is 5
+run: instruction main/1
+run: instruction main/2
+mem: storing 5 in location 3

//: Make sure that each routine gets a different alloc to start.
:(scenario new_concurrent)
recipe f1 [
  run f2:recipe
  1:address:integer/raw <- new integer:type
]
recipe f2 [
  2:address:integer/raw <- new integer:type
  # hack: assumes scheduler implementation
  3:boolean/raw <- equal 1:address:integer/raw, 2:address:integer/raw
]
+mem: storing 0 in location 3

//:: Next, extend 'new' to handle a string literal argument.

:(scenario new_string)
recipe main [
  1:address:array:character <- new [abc def]
  2:character <- index 1:address:array:character/deref, 5:literal
]
# integer code for 'e'
+mem: storing 101 in location 2

:(after "case NEW" following "Primitive Recipe Implementations")
if (current_instruction().ingredients[0].properties[0].second[0] == "literal-string") {
  // allocate an array just large enough for it
  vector<int> result;
  result.push_back(Current_routine->alloc);
  write_memory(current_instruction().products[0], result);
  // assume that all characters fit in a single location
//?   cout << "new string literal: " << current_instruction().ingredients[0].name << '\n'; //? 1
  Memory[Current_routine->alloc++] = current_instruction().ingredients[0].name.size();
  for (size_t i = 0; i < current_instruction().ingredients[0].name.size(); ++i) {
    Memory[Current_routine->alloc++] = current_instruction().ingredients[0].name[i];
  }
  // mu strings are not null-terminated in memory
  break;
}
