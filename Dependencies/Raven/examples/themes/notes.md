# Themes

A theme is **colors**, *metrics*, a material and a font.
Raven comes with four (`Dark`, `Light`, `GlassDark`, `GlassLight`);
a **theme file** adds yours.

## Styles

A style gives values to a *type* of widget, to a *class*, or to both:

```xml
<Style Widget="Button" CornerRadius="16"/>
<Style Class="danger" Accent="#FB7185"/>
```

- `Class="danger"` on a widget makes it take the second one.
- `Theme="Paper"` on a widget gives another theme to it and
  to what it contains: the card on the left does that.

## Live

- [x] `ocean.xml` and `paper.xml` are watched
- [x] so is `themes.xml`, this window
- [ ] try it: change an accent, save

> This text is a `Label` with `Markdown="true"`.
> The editor under it has the same flag: it styles
> what you type, where you type it.

| Role     | Example   |
|----------|-----------|
| Accent   | buttons   |
| Danger   | deletions |

[Open the documentation](action:themes.docs)
