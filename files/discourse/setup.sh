#!/bin/sh
cd /home/discourse/default
export RUBY_GC_MALLOC_LIMIT=90000000 RAILS_ENV=production
bundle exec rake db:migrate || exit 1
bundle exec rake assets:precompile || exit 1
bundle exec rails r 'user=User.find(-1);user.password="admin";user.save;user.email_tokens.each{|token|token.confirmed=true;token.save}' || exit 1
