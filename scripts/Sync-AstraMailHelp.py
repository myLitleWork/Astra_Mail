# -*- coding: utf-8 -*-
"""Synchronize the offline copy of built-in help; --check never writes files."""
from __future__ import print_function
import argparse,html,json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
PAGES=[('содержание','Руководство AstraMail'),('установка-и-обновление','Установка и обновление'),('первый-запуск-и-thunderbird','Первый запуск и Thunderbird'),('контакты-и-импорт','Контакты и импорт CSV'),('работа-с-письмами','Работа с письмами и флешками'),('настройки','Настройки и правила'),('фоновый-режим-и-связь','Фоновый режим и связь'),('данные-и-восстановление','Данные и восстановление'),('ошибки-и-диагностика','Ошибки и диагностика'),('встроенная-справка','Встроенная справка')]
HELP=[('localMailSettingsHelp','Настройки профиля'),('localContactsHelp','Адресная книга'),('subjectRulesHelp','Правила темы письма'),('attachmentGroupsHelp','Группы вложений'),('outgoingSubjectRulesHelp','Правила темы исходящего'),('idleSettingsHelp','Интервалы фонового режима')]
def render_page(name,body):
    i=[p[0] for p in PAGES].index(name);title=PAGES[i][1]
    links='\n'.join('<a '+('aria-current="page" ' if slug==name else '')+'href="'+slug+'.html">'+label+'</a>' for slug,label in PAGES)
    toc='<nav class="page-toc" aria-label="На этой странице"><strong>На этой странице</strong><ul>'+''.join('<li><a href="#'+anchor+'">'+text+'</a></li>' for anchor,text in re.findall(r'<h2 id="([^"]+)">([^<]+)</h2>',body))+'</ul></nav>'
    prev='<a href="'+PAGES[i-1][0]+'.html">← '+PAGES[i-1][1]+'</a>' if i else ''
    nxt='<a href="'+PAGES[i+1][0]+'.html">'+PAGES[i+1][1]+' →</a>' if i+1<len(PAGES) else ''
    return '<!doctype html>\n<html lang="ru"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>'+title+' — AstraMail</title><link rel="stylesheet" href="оформление.css"></head>\n<body><a class="skip" href="#текст">К тексту руководства</a><header><a href="содержание.html">AstraMail · Руководство пользователя</a><span>Astra Linux 1.6 · Офлайн</span></header>\n<div class="layout"><aside><nav aria-label="Разделы руководства"><div class="desktop-nav">'+links+'</div><details class="mobile-nav"><summary>Разделы руководства</summary>'+links+'</details></nav></aside><main id="текст"><h1>'+title+'</h1>'+toc+body+'<footer><nav class="neighbors" aria-label="Соседние страницы">'+prev+nxt+'</nav><p>Примеры вымышленные. Проверяйте операции на учебных данных; исходные материалы сохраняйте отдельно.</p></footer></main></div></body></html>\n'
def help_body():
    source=(ROOT/'src/Configs/Confg/ConfigDefaults.cpp').read_text(encoding='utf-8')
    parts=['<p class="lead">Эти шесть текстов совпадают со справками меню «Справка и туториалы». Их можно читать без запуска программы.</p><p>Практические шаги — в разделах <a href="настройки.html">настроек</a>, <a href="контакты-и-импорт.html">контактов</a> и <a href="фоновый-режим-и-связь.html">связи</a>.</p>']
    for name,title in HELP:
        m=re.search(r'const char\* '+name+r'\(\)\s*\{(.*?)\n    \}',source,re.S)
        if not m:raise ValueError(name)
        text=''.join(json.loads(t) for t in re.findall(r'"(?:\\.|[^"\\])*"',m.group(1)))
        parts.append('<h2 id="'+name+'">'+title+'</h2><pre><code>'+html.escape(text)+'</code></pre>')
    return '\n'.join(parts)
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--check',action='store_true');args=p.parse_args()
    target=ROOT/'tutorials/встроенная-справка.html';text=render_page('встроенная-справка',help_body())
    if args.check:
        if not target.is_file() or target.read_text(encoding='utf-8')!=text:raise SystemExit('Built-in help mirror differs')
    else:target.write_bytes(text.encode('utf-8'))
    print('BUILTIN_HELP_MIRRORS=6')
if __name__=='__main__':main()
