# -*- coding: utf-8 -*-
"""Validate the offline guide assets and navigation in a source tree or package."""
from __future__ import print_function
import os,re,sys
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote,urlsplit
class Page(HTMLParser):
    def __init__(self,text):
        HTMLParser.__init__(self);self.ids=set();self.refs=[];self.h1=0;self.feed(text)
    def handle_starttag(self,tag,attrs):
        a=dict(attrs)
        if 'id' in a:
            if a['id'] in self.ids:raise ValueError('Duplicate id: '+a['id'])
            self.ids.add(a['id'])
        self.h1+=tag=='h1'
        for key in ['href','src']:
            if key in a:self.refs.append((tag,a[key]))
def check(root):
    tutorials=root/'tutorials';pages={}
    required=['содержание','установка-и-обновление','первый-запуск-и-thunderbird','контакты-и-импорт','работа-с-письмами','настройки','фоновый-режим-и-связь','данные-и-восстановление','ошибки-и-диагностика','встроенная-справка']
    for name in required:
        p=tutorials/(name+'.html');text=p.read_text(encoding='utf-8');page=Page(text)
        if page.h1!=1 or '<html lang="ru">' not in text or 'charset="utf-8"' not in text:raise ValueError('Invalid page: '+name)
        pages[p.resolve()]=page
    refs=0
    for path,page in pages.items():
        for tag,ref in page.refs:
            url=urlsplit(ref)
            if url.scheme or url.netloc:
                if tag!='a' or url.scheme!='https':raise ValueError('Online asset: '+ref)
                continue
            target=Path(os.path.abspath(str(path.parent/unquote(url.path)))) if url.path else path
            if not target.is_file():
                sample=root/'deployment/AstraMailPortable/contacts-import-example.csv'
                if target==root.resolve()/'contacts-import-example.csv' and sample.is_file():target=sample
                else:raise ValueError('Broken link: '+str(path)+' -> '+ref)
            if url.fragment and (target not in pages or unquote(url.fragment) not in pages[target].ids):raise ValueError('Missing anchor: '+ref)
            refs+=1
    if len(list(tutorials.iterdir()))!=11:raise ValueError('Unexpected legacy or extra tutorial files')
    print('OFFLINE_TUTORIALS=10 LINKS_VERIFIED='+str(refs),flush=True)
if __name__=='__main__':check(Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parent.parent)
