# Check our support for fake file systems in scenarios.

scenario read-from-fake-file [
  local-scope
  assume-resources [
    [a] <- [
      |xyz|
    ]
  ]
  contents:&:source:char <- start-reading resources, [a]
  1:char/raw <- read contents
  2:char/raw <- read contents
  3:char/raw <- read contents
  4:char/raw <- read contents
  _, 5:bool/raw <- read contents
  memory-should-contain [
    1 <- 120  # x
    2 <- 121  # y
    3 <- 122  # z
    4 <- 10  # newline
    5 <- 1  # eof
  ]
]

scenario write-to-new-fake-file [
  local-scope
  assume-resources [
  ]
  sink:&:sink:char, writer:num/routine <- start-writing resources, [a]
  sink <- write sink, 120/x
  sink <- write sink, 121/y
  close sink
  wait-for-routine writer
  contents-read-back:text <- slurp resources, [a]
  10:bool/raw <- equal contents-read-back, [xy]
  memory-should-contain [
    10 <- 1  # file contents read back exactly match what was written
  ]
]

scenario write-to-new-fake-file-2 [
  local-scope
  assume-resources [
    [a] <- [
      |abc|
    ]
  ]
  sink:&:sink:char, writer:num/routine <- start-writing resources, [b]
  sink <- write sink, 120/x
  sink <- write sink, 121/y
  close sink
  wait-for-routine writer
  contents-read-back:text <- slurp resources, [b]
  10:bool/raw <- equal contents-read-back, [xy]
  memory-should-contain [
    10 <- 1  # file contents read back exactly match what was written
  ]
]

scenario write-to-fake-file-that-exists [
  local-scope
  assume-resources [
    [a] <- []
  ]
  sink:&:sink:char, writer:num/routine <- start-writing resources, [a]
  sink <- write sink, 120/x
  sink <- write sink, 121/y
  close sink
  wait-for-routine writer
  contents-read-back:text <- slurp resources, [a]
  10:bool/raw <- equal contents-read-back, [xy]
  memory-should-contain [
    10 <- 1  # file contents read back exactly match what was written
  ]
]

scenario write-to-existing-file-preserves-other-files [
  local-scope
  assume-resources [
    [a] <- []
    [b] <- [
      |bcd|
    ]
  ]
  sink:&:sink:char, writer:num/routine <- start-writing resources, [a]
  sink <- write sink, 120/x
  sink <- write sink, 121/y
  close sink
  wait-for-routine writer
  contents-read-back:text <- slurp resources, [a]
  10:bool/raw <- equal contents-read-back, [xy]
  other-file-contents:text <- slurp resources, [b]
  11:bool/raw <- equal other-file-contents, [bcd
]
  memory-should-contain [
    10 <- 1  # file contents read back exactly match what was written
    11 <- 1  # other files also continue to persist unchanged
  ]
]
