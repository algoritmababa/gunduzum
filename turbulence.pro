TEMPLATE = subdirs

SUBDIRS = core app cli tests

app.depends = core
cli.depends = core
tests.depends = core
