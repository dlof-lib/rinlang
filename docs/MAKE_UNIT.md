# Make Unit

`@make.(name)` is Rin's general-purpose buildable unit. It combines executable Rin code with a policy describing what the unit is allowed or required to use.

```rin
@make.(dashboard)
    kind page;
    use container;
    use loop;
    need container;
    strict;

    fun render(items)
        for (let i = 0; i < 3; i = i + 1) {
            print(items[i]);
        }
    end

    @container.body
        let title = "Dashboard";
    .end/container
.end/make=dashboard
```

## Directives

- `kind app|page|component|library|module|service|task|data|plugin;`
- `use capability;` declares an allowed capability in strict mode.
- `need capability;` requires that the capability is actually used.
- `allow capability;` creates an explicit whitelist.
- `deny capability;` blocks a capability.
- `input name;` / `output name;` declare unit contract names.
- `public name;` / `private name;` declare API intent for tooling.
- `version "1.0.0";` and `description "...";` add metadata.
- `strict;` requires every detected capability to be declared with `use`.

Capabilities include `container`, `loop`, `function`, `condition`, `return`, `view`, `data`, `api`, `import`, `table`, `doc`, `chatbot`, and `reckon`.

The policy is validated before the Make Unit body executes. Existing `@make=name` remains backward-compatible.
