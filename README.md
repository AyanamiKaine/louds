# Louds Library

Based on the ideas of Anton Mikhailov. As seen here [Avoiding Modern C++ | Anton Mikhailov](https://www.youtube.com/watch?v=ShSGHb65f3M) and [Still avoiding modern C++](https://www.youtube.com/watch?v=-m7lhJ_Mzdg&t)

The basic idea is just having a pool of fat structs and using intrusive data structures to model a game world. We don't use any pointers to model relationships between the data. This makes it possible to serialize the entire game state trivially. As pointers are instead a flat index into an array.