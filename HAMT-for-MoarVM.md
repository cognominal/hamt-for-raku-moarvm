## blurb

We discuss here how to use a modified HAMT with persistable tries and reference
counted `table`s to create MoarVM reprs for persistent hashes and stack.
This is part of a work tu support treesitters for raku slangs.


We want to modify `Hamt` to implement a tree sitter with acceptable
performance for raku. That is, to adapt `Hamt` to raku MoarVM based GC. See
[why](#why) we need to do that. A modified `Hamt` is added as a git module to the
MoarVM git repo, and 6model representations (reprs) are added to MoarVM.

`Hamt` denotes the original [implementation](https://github.com/mkirchner/hamt)
intended to support immutable hashes. `hamt` withtout the capital denotes the
`hamt` structure. `Hamt` can be trivially adapted to support immutable stacks
as well (as opposed to arrays which implies insertion). `hamt-raku` denotes the
present adaptation.

We discuss here only the part involving MoarVM reprs, not how it used. So the
adaptations of raku and vscode to exploit data from the persistent world
provided by hamt-raku will be treated/discussed elsewhere.

## initial goal

At this point, we want to create persistable reprs for hash and stacks

Note : I initially planned to dynamically add hamt reprs to MoarVM to avoid to
depend on a modified version of this git repo. But, it is necessary anyway to
modify MoarVM to support iterators and reference counting for the news reprs.

And also create a nqp op, also dynamically loaded, that
freeze mutable structures of the world. 

## Prerequisite to read this doc

Rakudo is the implementation of raku. Nqp is a simplified raku used to
implement raku. MoarVM is the preferred raku backend written in C. See below,
[hamt README](https://github.com/mkirchner/hamt/blob/main/README.md) and
[rakudo](https://github.com/rakudo/rakudo/tree/main/docs)/[nqp](https://github.com/Raku/nqp/tree/main/docs)/[MoarVM](https://github.com/MoarVM/MoarVM/tree/main/docs)
docs for definitions of the terms used in this initial blurb.


## Why

[Treesitter](https://en.wikipedia.org/wiki/Tree-sitter_(parser_generator))
based syntaxic highlighting of raku and its slangs, possibly changing on the
fly, necessitates an approach different from existing ones. With the fluidity
of raku syntax due to slangs, using a specialized parser for that purpose, as
is now the norm, is a non starter. Instead we will use the existing parser but
use a shared persistent structure so to be able to restart parsing from any
point where the user edits code.

## Being pragmatic

The whole point of treesitters is being responsive to provide timely syntactic
highlighting when editing code, so we must be careful of the impact of the
implementation.

# The doc per se

So we want to use
[persistent](https://en.wikipedia.org/wiki/Persistent_data_structure)
[tries](https://en.wikipedia.org/wiki/Trie), that is,
[hamts](https://en.wikipedia.org/wiki/Hash_array_mapped_trie) to compactly
store the history of the
[world](https://github.com/Raku/nqp/blob/main/src/NQP/World.nqp).

The world is holding the state of raku/npq compilation. the World is
constituted of hashes and stacks so it can be implemented in term of hamt
tries. 

We want minimal impact on existing code, be it MoarVM or nqp, except may be in
World.pm. It implies creating new MoarVM representations to implement
persistent hashes and stacks to support a (not so) brave new world. We want to
avoid changing the structure of [6 model
representations](https://github.com/Raku/nqp/blob/main/docs/6model/overview.markdown#representations)
(reprs), that is avoiding adding new methods in
[MVMREPROps](https://github.com/MoarVM/MoarVM/blob/8c413c4f0ce0fed7c2accf92db496112c205a206/src/6model/6model.h#L561)
which instances acts as a
[vtable](https://en.wikipedia.org/wiki/Dispatch_table). For simplicity we don't
care about implementing concurrent persistent structures like is done in
[ctries](https://en.wikipedia.org/wiki/Ctrie).


##  Conventions

The present doc describes Hamt code that is included when the HAMT_HYBRID cpp value is set.

We denote by `table`, `hamt_node.table`. That is the table part for the
hamt_node union. We denote by `kv`, the other part of the union :
`hamt_node.kv`. In addition the tag `HAMT_TAG_VALUE`we use `HAMT_TAG_FROZEN`.
When set on a table is means all its descendants are frozen. `kw`s are not
affected by the frozen tag.

  #define HAMT_TAG_FROZEN 0x2
  #define frozen(__p) (struct hamt_node *)((uintptr_t)__p | HAMT_TAG_FROZEN)
  #define is_frozen(__p) (((uintptr_t)__p & HAMT_TAG_MASK) == HAMT_TAG_FROZEN)




## not changing reprs structure

Using only persistent structures has two drawbacks:

* Each repr hash/array operation which previously was a mutation, would return a new
persistent structure. This implies adding new entries to MVMREPROps to support
methods that returns that value. Growing `MVMREPROp`s would impact the memory
caches. 

* Creating a persistent structure for each intermediate state of an
`hamt` instead of a mutation implies a lot of path copying meaning using a lot
of memory.

Instead we want to store peristent state just at some chosen point to avoid
using too much memory.

Hamt currently proposes a dichotomy between ephemeral and persistent `hamt`
that does not fit our purpose (unchanged repr interface). We introduce the new
(?) concept of persistable or hybrid `hamt` which is a mix of an ephemeral and
persistent `hamt` behavior. We do so by introducing a new flag in the taggable
pointer that, when set, denotes a frozen `table`. A frozen `table` has all its
sub`table`s, if any, frozen. When mutating a persistable `hamt`, we can mutate
it like an ephemeral `hamt` if the `table` involved in the mutation is not
frozen. Otherwise we duplicate the frozen `table` to create an unfrozen
modified `table`, and we do the appropriate path copying. In both cases a `kw`
is created or deleted depending on the operation. These operations do no create
a new `hamt`, this done by which points to the same root `hamt_node`.


Hybrid is preferred to persistable in code because its first letter is distinctive.
The code for hybrid hamt is compiled if the preprocessor HAMT_HYBRID is set.

The exposed API is `hamt_hset`, `hamt_hrem`, `hamt_hfreeze`. Note the `h``
to distinguish these functions from their non hybrid counterpart.
Internal API is `hamt_it_hcreate`.


## GC 

The whole point of this adaptation is to support MoarVM GC. We dont want to
negatively impact GC. So we probably eventually use use reference counting for
`hamt_node`s so only the root of tries need to be marked by the mark phase of
GC.


## reference counting

With reference counting the GC mark phase would not mark stuff beyond the root
of our structures. As a result, values in these structures would be wrongly
garbage collected. So we need to mark them so the GC would know not to free
them. That means modifying the code of MoarVM.


`hamt_freeze` freezes an `hamt`. It uses a special
`hamt_it_next_unfrozen`


Freezing a mutable hamt structure means to create a immutable hamt structure meaning 
create the appropriate constant hamt_nodes with the appropriate path copying from the 
mutable hamt_nodes.

The part involving reference counting are irrelevant for the pure GC implementation.

A mutable trie root is a mutable table. A child of a mutable table can be
either mutable or immutable. A children of an immutable table is mutable. A
mutable table is not shared and is identified by a count field of 2^64 - 1 for
reference counting The root table of an immutable subtree has a `count` field
which value is the count of tries that share it.

Freeing a immutable trie decrement the count field of its tables.
If a table count field become 0, this table and its descendants are freed.

A immutable trie is made from a mutable one by duplicating the root table
converting all the mutable table to immutable one with a `count` field set to 1.

## World.pm, QAST and MAST

When updating World.nqp, code involving hashes and arrays are
translated in QAST::VarWithFallBack which are processed in still mysterious
way for me.

idea. Doubled sigil would mean hamt hash/array. 
Code of the form

  %%b := %%a{'foo'} := 'bar';

would be short for  
  
  %%a{'foo'} := 'bar';   # update the hamt mutable %%a
  %%b := %%a;            # freeze %%a into %%b

Unclear how to process that in QAST/MAST


## implementation details

now just a scaffolding of things to come.

I now start from a APP::Mi6 scaffolded raku repo where I copied the hamt implementation.
My hope of going with changes to MoarVM are dashed. I will need to modify the MVMIter repr.

Initial goal :  implement the hamt-node repr and test if from nqp.


Apparently [LibraryMake](https://raku.land/zef:jjmerelo/LibraryMake) is the
way to "simplify building native code for a Raku module".

[register_repr](https://github.com/MoarVM/MoarVM/blob/e149d3de5bf1514008401d0f63f040e03f057217/src/6model/reprs.c#181)
register a repr. Apparently there is no nqp op that wraps it.

MVM_register_extop. Example of 
[use](https://github.com/rakudo/rakudo//blob/969ae326d9bca4e4d5b0906f4d53e76bf702e020/src/vm/moar/ops/perl6_ops.c)


`loader.raku` will contain code to load the reprs and the freeze op

`reprs/` contains the repts
* `reprs/hamt-array.[hc]`
* `reprs/hamt-hash.[hc]`
* `reprs/hamt-node.[hc]`
`hamt` contains the hamt code independannt from MoarVM

The initial idead was that hamt-node.c will contain no method except for garbage collecting.
In fact it will have array indexing for testing.

## hatmt-node repr

It is the working hotse of hamt-array and hamt-hash
It wraps [struct hamt](src/hamt/internal_types.h)