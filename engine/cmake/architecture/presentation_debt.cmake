# Presentation code still written in the old object style (stateful
# directors, per-entity maps). ECS is the only accepted architecture: each of
# these is to be migrated to side-table components on the simulation's
# entities plus stateless presentation systems, and then removed from this
# list. The list may only shrink: a file listed here that no longer matches
# fails the check (remove it), and no new file may be added.
set(presentation_debt)
