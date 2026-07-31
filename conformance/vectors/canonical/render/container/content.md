# Canonical Render {#top}

Prose with *emphasis*, **strong**, `a & b`, a [link](https://example.org/doc),
an autolink <https://example.org/auto>, an entity &copy;, and text that looks
like markup: <div>not a tag</div>.

## Lists {#lists}

- tight one
- tight two
  - nested tight

1. loose first

2. loose second

> A quote, wrapped
> across two source lines.

```sh
mcdf render html doc/ -o doc.html
```

A hard break lives here  
and the line after it.

![Sized](assets/diagram.png "width=480 align=center")

![Captioned](assets/diagram.png "A plain caption")

---
