# example program: reading events from keyboard or mouse
#
# Keeps printing 'a' until you press a key or click on the mouse.

def main [
  local-scope
  open-console
  {
    e:event, found?:bool <- check-for-interaction
    break-if found?
    print-character-to-display 97, 7/white
    loop
  }
  close-console
  $print e, 10/newline
]
